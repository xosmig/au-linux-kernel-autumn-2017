#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/sched/signal.h>

#include "stack.h"
#include "assert.h"

static int __init test_stack(void)
{
    int ret = 0;
    LIST_HEAD(data_stack);
    stack_entry_t *tos = NULL;
    const char *tos_data = NULL;
    const char* test_data[] = { "1", "2", "3", "4" };
    long i = 0;

    pr_alert("Testing basic stack");

    for (i = 0; i != ARRAY_SIZE(test_data); ++i) {
        tos = create_stack_entry((void*)test_data[i]);
        if (!tos) {
            ret = -ENOMEM;
            break;
        }
        stack_push(&data_stack, tos);
    }

    for (i = ARRAY_SIZE(test_data) - 1; i >= 0; --i) {
        tos = stack_pop(&data_stack);
        tos_data = STACK_ENTRY_DATA(tos, const char*);
        delete_stack_entry(tos);
        printk(KERN_ALERT "%s == %s\n", tos_data, test_data[i]);
        assert(!strcmp(tos_data, test_data[i]));
    }

    assert(stack_empty(&data_stack));
    if (ret == 0)
        pr_alert("Test success!\n");

    return ret;
}

static int __init print_processes_backwards(void)
{
    int error_code = 0;
    struct task_struct* iter;
    LIST_HEAD(stack);

    for_each_process(iter) {
        char *comm = kmalloc(sizeof(char) * TASK_COMM_LEN, GFP_KERNEL);
        stack_entry_t *new = create_stack_entry(comm);
        if (!comm || !new) {
            error_code = -ENOMEM;
            kfree(comm);
            kfree(new);
            break;
        }

        get_task_comm(comm, iter);
        stack_push(&stack, new);
    }

    while (!stack_empty(&stack)) {
        stack_entry_t *tos = stack_pop(&stack);
        char *comm = STACK_ENTRY_DATA(tos, char*);
        if (!error_code) {
          printk(KERN_ALERT "%s", comm);
        }
        delete_stack_entry(tos);
        kfree(comm);
    }
    return error_code;
}

static int __init ll_init(void)
{
    int ret = 0;
    printk(KERN_ALERT "Hello, linked_lists\n");

    ret = test_stack();
    if (ret)
        goto error;

    ret = print_processes_backwards();

error:
    return ret;
}

static void __exit ll_exit(void)
{
    printk(KERN_ALERT "Goodbye, linked_lists!\n");
}

module_init(ll_init);
module_exit(ll_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Linked list exercise module");
MODULE_AUTHOR("Kernel hacker!");
