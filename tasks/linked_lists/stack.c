#include "stack.h"
#include <linux/slab.h>
#include <linux/gfp.h>

stack_entry_t *create_stack_entry(void *data)
{
    LIST_HEAD(lh);
    stack_entry_t *new = kmalloc(sizeof(*new), GFP_KERNEL);
    if (new) {
        new->lh = lh;
        new->data = data;
    }
    return new;
}

void delete_stack_entry(stack_entry_t *entry)
{
    kfree(entry);
}

void stack_push(struct list_head *stack, stack_entry_t *entry)
{
    list_add(&entry->lh, stack);
}

stack_entry_t* stack_pop(struct list_head *stack)
{
    stack_entry_t* res = (stack_entry_t*) stack->next;
    list_del(&res->lh);
    return res;
}
