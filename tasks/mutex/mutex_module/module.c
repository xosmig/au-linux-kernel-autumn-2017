#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include "mutex_ioctl.h"

#define LOG_TAG "[MUTEX_MODULE] "

typedef struct core_mutex_state {
    mutex_id_t mutex_id;

    wait_queue_head_t lock_wait;

    struct kref refcnt;
    atomic_t destroyed;

    struct hlist_node lnode;
} core_mutex_state_t;

static void init_core_mutex_state(core_mutex_state_t *cmstate)
{
    atomic_set(&cmstate->destroyed, 0);
    wmb();
    kref_init(&cmstate->refcnt);
    init_waitqueue_head(&cmstate->lock_wait);
}

// called by the reference counter
static void release_core_mutex_state(struct kref *ref)
{
    core_mutex_state_t *data = container_of(ref, core_mutex_state_t, refcnt);
    kfree(data);
}

// WARNING: Don't call `kfree` or decrement the reference counter after this function call
static void deinit_core_mutex_state(core_mutex_state_t *cmstate)
{
    atomic_set(&cmstate->destroyed, 1);
    wmb();
    wake_up_all(&cmstate->lock_wait);
    // the memory will be freed by the reference counter
    // when all waiting threads are awakened
    kref_put(&cmstate->refcnt, release_core_mutex_state);
}

typedef struct task_group_mutex_state {
    struct file *filp;

    spinlock_t wlock;
    mutex_id_t mutex_cnt;
    struct hlist_head cmstates;

    struct hlist_node lnode;
} task_group_mutex_state_t;

static void init_task_group_mutex_state(task_group_mutex_state_t *tgstate, struct file *filp)
{
    tgstate->filp = filp;
    spin_lock_init(&tgstate->wlock);
    tgstate->mutex_cnt = 0;
    INIT_HLIST_HEAD(&tgstate->cmstates);
}

static void deinit_task_group_mutex_state(task_group_mutex_state_t *tgstate)
{
    core_mutex_state_t *cmstate;
    struct hlist_node *tmp;
    hlist_for_each_entry_safe(cmstate, tmp, &tgstate->cmstates, lnode) {
        deinit_core_mutex_state(cmstate);
        hlist_del(&cmstate->lnode);
        // NOTE: the memory will be freed by the reference counter
    }
}

typedef struct system_mutex_state {
    spinlock_t wlock;
    struct hlist_head tgstates;
} system_mutex_state_t;

typedef struct mutex_dev {
    struct miscdevice mdev;
    system_mutex_state_t sysmstate;
} mutex_dev_t;

static mutex_dev_t *mutex_dev;

static void init_system_mutex_state(system_mutex_state_t *sysmstate)
{
    spin_lock_init(&sysmstate->wlock);
    INIT_HLIST_HEAD(&sysmstate->tgstates);
}

static void deinit_system_mutex_state(system_mutex_state_t *sysmstate)
{
    // This is called on module release. So no opened file descriptors
    // exist. Thus we have nothing to cleanup here
}

static task_group_mutex_state_t *find_tgstate_rcu(struct file *filp)
{
    task_group_mutex_state_t *tgstate;
    hlist_for_each_entry_rcu(tgstate, &mutex_dev->sysmstate.tgstates, lnode) {
        if (tgstate->filp == filp) {
            return tgstate;
        }
    }
    return NULL;
}

static core_mutex_state_t *find_cmstate_rcu(task_group_mutex_state_t *tgstate, mutex_id_t mutex_id)
{
    core_mutex_state_t *cmstate;
    hlist_for_each_entry_rcu(cmstate, &tgstate->cmstates, lnode) {
        if (cmstate->mutex_id == mutex_id) {
            return cmstate;
        }
    }
    return NULL;
}

static int mutex_dev_open(struct inode *inode, struct file *filp)
{
    task_group_mutex_state_t *tgstate = kmalloc(sizeof(*tgstate), GFP_KERNEL);
    if (!tgstate) {
        return -ENOMEM;
    }

    init_task_group_mutex_state(tgstate, filp);

    spin_lock(&mutex_dev->sysmstate.wlock);
    hlist_add_head_rcu(&tgstate->lnode, &mutex_dev->sysmstate.tgstates);
    spin_unlock(&mutex_dev->sysmstate.wlock);
    tgstate = NULL;  // tgstate is not protected anymore

    pr_notice(LOG_TAG " opened successfully\n");
    return 0;
}

static int mutex_dev_release(struct inode *inode, struct file *filp)
{
    task_group_mutex_state_t *tgstate = NULL;

    spin_lock(&mutex_dev->sysmstate.wlock);
    tgstate = find_tgstate_rcu(filp);
    // assert(tgstate)
    hlist_del_rcu(&tgstate->lnode);
    spin_unlock(&mutex_dev->sysmstate.wlock);
    // we own tgstate

    synchronize_rcu();
    deinit_task_group_mutex_state(tgstate);

    pr_notice(LOG_TAG " closed\n");
    return 0;
}

static long mutex_lock_destroy(struct file *filp, mutex_id_t mutex_id);

static long mutex_ioctl_lock_create(struct file *filp, mutex_ioctl_lock_create_arg_t __user *uarg)
{
    task_group_mutex_state_t *tgstate = NULL;
    mutex_ioctl_lock_create_arg_t arg;
    core_mutex_state_t *cmstate = NULL;

    if (copy_from_user(&arg, uarg, sizeof(arg))) {
        return -EFAULT;
    }

    cmstate = kmalloc(sizeof(*cmstate), GFP_KERNEL);
    if (!cmstate) {
        return -ENOMEM;
    }
    init_core_mutex_state(cmstate);

    // === Begin RCU section ===
    rcu_read_lock();

    tgstate = find_tgstate_rcu(filp);
    // assert(tgstate);

    spin_lock(&tgstate->wlock);
    cmstate->mutex_id = tgstate->mutex_cnt++;
    hlist_add_head_rcu(&cmstate->lnode, &tgstate->cmstates);
    arg.id = cmstate->mutex_id;
    spin_unlock(&tgstate->wlock);
    cmstate = NULL;  // cmstate is not protected anymore
    rcu_read_unlock();
    tgstate = NULL;  // tgstate is unavailable after the end of the RCU section
    // === End RCU section ===

    if (copy_to_user(uarg, &arg, sizeof(arg))) {
        // OH NO! Have to undo all the work!
        mutex_lock_destroy(filp, arg.id);
        // assert(returned 0)
        return -EFAULT;
    }

    return 0;
}

static long mutex_lock_destroy(struct file *filp, mutex_id_t mutex_id)
{
    task_group_mutex_state_t *tgstate = NULL;
    core_mutex_state_t *cmstate = NULL;

    // === Begin RCU section ===
    rcu_read_lock();

    tgstate = find_tgstate_rcu(filp);
    // assert(tgstate);

    spin_lock(&tgstate->wlock);
    cmstate = find_cmstate_rcu(tgstate, mutex_id);
    if (!cmstate) {
        spin_unlock(&tgstate->wlock);
        rcu_read_unlock();
        return -EINVAL;
    }
    hlist_del_rcu(&cmstate->lnode);
    spin_unlock(&tgstate->wlock);
    // we own cmstate

    rcu_read_unlock();
    tgstate = NULL;  // tgstate is unavailable after the end of the RCU section
    // === End RCU section ===

    synchronize_rcu();
    deinit_core_mutex_state(cmstate);
    // NOTE: the memory will be freed by the reference counter

    return 0;
}

static long mutex_ioctl_lock_destroy(struct file *filp, mutex_ioctl_lock_destroy_arg_t __user *uarg)
{
    mutex_ioctl_lock_destroy_arg_t arg;

    if (copy_from_user(&arg, uarg, sizeof(arg))) {
        return -EFAULT;
    }

    return mutex_lock_destroy(filp, arg.id);
}

static long mutex_ioctl_sleep(struct file *filp, mutex_ioctl_sleep_arg_t __user *uarg)
{
    mutex_ioctl_sleep_arg_t arg;
    task_group_mutex_state_t *tgstate = NULL;
    core_mutex_state_t *cmstate = NULL;
    int ret = 0;

    if (copy_from_user(&arg, uarg, sizeof(arg))) {
        return -EFAULT;
    }

    // === Begin RCU section ===
    rcu_read_lock();
    tgstate = find_tgstate_rcu(filp);
    cmstate = find_cmstate_rcu(tgstate, arg.id);
    if (!cmstate) {
        rcu_read_unlock();
        return -EINVAL;
    }
    kref_get(&cmstate->refcnt);
    rcu_read_unlock();
    // NOTE: cmstate is used outside the RCU section on purpose
    // it's guaranteed not to be freed by means of reference counting
    // === End RCU section ===

    ret = wait_event_interruptible_exclusive(
        cmstate->lock_wait,
        atomic_read(&cmstate->destroyed) || shared_spin_trylock_once(arg.shared_spinlock));

    // rmb is done by `wait_event_interruptible_exclusive` call
    if (!ret && atomic_read(&cmstate->destroyed)) {
        ret = -EINTR;
    }

    kref_put(&cmstate->refcnt, release_core_mutex_state);
    return ret;
}

static long mutex_ioctl_wakeup(struct file *filp, mutex_ioctl_wakeup_arg_t __user *uarg)
{
    mutex_ioctl_wakeup_arg_t arg;
    task_group_mutex_state_t *tgstate = NULL;
    core_mutex_state_t *cmstate = NULL;

    if (copy_from_user(&arg, uarg, sizeof(arg))) {
        return -EFAULT;
    }

    // === Begin RCU section ===
    rcu_read_lock();
    tgstate = find_tgstate_rcu(filp);
    cmstate = find_cmstate_rcu(tgstate, arg.id);
    if (!cmstate) {
        rcu_read_unlock();
        return -EINVAL;
    }
    wake_up(&cmstate->lock_wait);
    rcu_read_unlock();
    // === End RCU section ===

    return 0;
}

static long mutex_dev_ioctl(struct file *filp, unsigned int cmd,
        unsigned long arg)
{
    switch(cmd) {
        case MUTEX_IOCTL_LOCK_CREATE:
            return mutex_ioctl_lock_create(filp, (mutex_ioctl_lock_create_arg_t __user*)arg);
            break;
        case MUTEX_IOCTL_LOCK_DESTROY:
            return mutex_ioctl_lock_destroy(filp, (mutex_ioctl_lock_destroy_arg_t __user*)arg);
            break;
        case MUTEX_IOCTL_SLEEP:
            return mutex_ioctl_sleep(filp, (mutex_ioctl_sleep_arg_t __user*)arg);
            break;
        case MUTEX_IOCTL_WAKEUP:
            return mutex_ioctl_wakeup(filp, (mutex_ioctl_wakeup_arg_t __user*)arg);
            break;
        default:
            return -ENOTTY;
    }
}

static struct file_operations mutex_dev_fops = {
    .owner = THIS_MODULE,
    .open = mutex_dev_open,
    .release = mutex_dev_release,
    .unlocked_ioctl = mutex_dev_ioctl
};

static int __init mutex_module_init(void)
{
    int ret = 0;
    mutex_dev = (mutex_dev_t*)
        kzalloc(sizeof(*mutex_dev), GFP_KERNEL);
    if (!mutex_dev) {
        ret = -ENOMEM;
        pr_warn(LOG_TAG "Can't allocate memory\n");
        goto error_alloc;
    }
    mutex_dev->mdev.minor = MISC_DYNAMIC_MINOR;
    mutex_dev->mdev.name = "mutex";
    mutex_dev->mdev.fops = &mutex_dev_fops;
    mutex_dev->mdev.mode = S_IRUSR | S_IRGRP | S_IROTH
        | S_IWUSR| S_IWGRP | S_IWOTH;
    init_system_mutex_state(&mutex_dev->sysmstate);

    if ((ret = misc_register(&mutex_dev->mdev)))
        goto error_misc_reg;

    pr_notice(LOG_TAG "Mutex dev with MINOR %u"
        " has started successfully\n", mutex_dev->mdev.minor);
    return 0;

error_misc_reg:
    kfree(mutex_dev);
    mutex_dev = NULL;
error_alloc:
    return ret;
}

static void __exit mutex_module_exit(void)
{
    pr_notice(LOG_TAG "Removing mutex device %s\n", mutex_dev->mdev.name);
    misc_deregister(&mutex_dev->mdev);
    deinit_system_mutex_state(&mutex_dev->sysmstate);
    kfree(mutex_dev);
    mutex_dev = NULL;
}

module_init(mutex_module_init);
module_exit(mutex_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AU user space mutex kernel side support module");
MODULE_AUTHOR("Kernel hacker!");
