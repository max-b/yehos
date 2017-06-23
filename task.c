#include "task.h"
#include "virtualmem.h"
#include "asmhelpers.h"
#include "memlib.h"

#define NUM_TASKS 16
#define SWAP_STACK 0xffbfe000

uint32_t swap_page[0x1000 >> 2];

context_t all_tasks[NUM_TASKS];
int current_task = 0;

int
is_ready_task(int tasknum)
{
    context_t *ctx = &all_tasks[tasknum];
    return ctx->ready;
}

/* void */
/* save_context(int tasknum, void *eip) */
/* { */
/*     context_t *ctx = &all_tasks[tasknum]; */
/*     asm_save_context(ctx, eip); */
/* } */
/*  */
void
switch_executing_task(int target_task_num)
{
    context_t *stale_context = &all_tasks[current_task];
    context_t *target_context = &all_tasks[target_task_num];

    stale_context->ready = 1;
    asm_switch_to(stale_context, target_context);

    memset(stale_context, 0, sizeof(*stale_context));
}

// Wraps asm_fork
/* int */
/* fork() { */
/*  */
/* } */

void
yield(void)
{
    for (int i=current_task+1; i < current_task+NUM_TASKS; i++) {
        int tnum = i % NUM_TASKS;
        if (is_ready_task(tnum)) {
            switch_executing_task(tnum);
        }
    }
}

void
idle() {
    while (1) {
        // TODO put spinny on visible screen
        yield();
    }
}

int
ptable_is_present(pagetable_entry_t entry) {
  return entry & 1;
}

void
make_ptable_entries_cow(ptable_index_t start_index) {
    ptable_index_t stack_ptable_index = BASE_OF_VIRTUAL_STACK >> 12;
    virtaddr_t *base_ptable = (void *) 0xffc00000;

    uint32_t page_size = 1024;
    for (int i = start_index; i < start_index + page_size; i++) {
        if (i != stack_ptable_index) {
            base_ptable[i] = make_cow(base_ptable[i]);
        }
    }
}

void
clone_page_directory(context_t *new_ctx)
{
    virtaddr_t *current_pagedir = (void *) 0xfffff000;

    // Insert our new pagedir into the current address space so we can write to it.
    physaddr_t new_cr3 = get_unused_page();
    virtaddr_t *new_pagedir = 0xffbfd000;
    set_ptable_entry((virtaddr_t) new_pagedir, new_cr3);

    // Copy stack to swap page.
    // We're assuming the stack only occupies a single page.
    physaddr_t new_phys_stack = get_unused_page();
    set_ptable_entry(SWAP_STACK, new_phys_stack);
    memcpy((void *) SWAP_STACK, (void *) BASE_OF_VIRTUAL_STACK, STACK_SIZE);

    // Make cow from the first non-identity mapped page up to (but not including) the page directory itself.
    ptable_index_t first_nonident_ptable_idx = 256;
    uint32_t page_size = 1024;
    virtaddr_t *base_ptable = (void *) 0xffc00000;
    for (int i = first_nonident_ptable_idx; i < page_size-1; ++i) {
        if (ptable_is_present(current_pagedir[i])) {
            make_ptable_entries_cow((ptable_index_t) i*page_size);
        }
        current_pagedir[i] = new_pagedir[i] = make_cow(current_pagedir[i]);
    }

    // the physical swap stack becomes the real stack for the current address space
    set_ptable_entry(BASE_OF_VIRTUAL_STACK, new_phys_stack);

    new_pagedir[1023] = make_present_and_rw(new_cr3);
    new_ctx->cr3 = new_cr3;
}

void
test_clone_page_directory()
{
    context_t *ctx;
    clone_page_directory(ctx);
}

