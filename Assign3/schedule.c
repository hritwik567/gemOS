#include<context.h>
#include<init.h>
#include<memory.h>
#include<schedule.h>
#include<apic.h>
#include<lib.h>
#include<idt.h>

#define omap(u)     (u64*)osmap(u)

static u64 numticks;
static u64 to_free_page[16] = {0x0};

static void save_and_restore_timer(struct exec_context* current, struct exec_context* next, u64* rsp14, u64* orbp)
{
    //Saving current registers
    current->regs.rax = *(rsp14 + 0);
    current->regs.rbx = *(rsp14 + 1);
    current->regs.rcx = *(rsp14 + 2);
    current->regs.rdx = *(rsp14 + 3);
    current->regs.rsi = *(rsp14 + 4);
    current->regs.rdi = *(rsp14 + 5);

    current->regs.rbp = *(orbp + 0);

    current->regs.r8 = *(rsp14 + 6);
    current->regs.r9 = *(rsp14 + 7);
    current->regs.r10 = *(rsp14 + 8);
    current->regs.r11 = *(rsp14 + 9);
    current->regs.r12 = *(rsp14 + 10);
    current->regs.r13 = *(rsp14 + 11);
    current->regs.r14 = *(rsp14 + 12);
    current->regs.r15 = *(rsp14 + 13);

    current->regs.entry_rip = *(orbp + 1);
    current->regs.entry_cs = *(orbp + 2);
    current->regs.entry_rflags = *(orbp + 3);
    current->regs.entry_rsp = *(orbp + 4);
    current->regs.entry_ss = *(orbp + 5);

    //Restoring next registers
    *(rsp14 + 0) = next->regs.rax;
    *(rsp14 + 1) = next->regs.rbx;
    *(rsp14 + 2) = next->regs.rcx;
    *(rsp14 + 3) = next->regs.rdx;
    *(rsp14 + 4) = next->regs.rsi;
    *(rsp14 + 5) = next->regs.rdi;

    *(orbp + 0) = next->regs.rbp;

    *(rsp14 + 6) = next->regs.r8;
    *(rsp14 + 7) = next->regs.r9;
    *(rsp14 + 8) = next->regs.r10;
    *(rsp14 + 9) = next->regs.r11;
    *(rsp14 + 10) = next->regs.r12;
    *(rsp14 + 11) = next->regs.r13;
    *(rsp14 + 12) = next->regs.r14;
    *(rsp14 + 13) = next->regs.r15;

    *(orbp + 1) = next->regs.entry_rip;
    *(orbp + 2) = next->regs.entry_cs;
    *(orbp + 3) = next->regs.entry_rflags;
    *(orbp + 4) = next->regs.entry_rsp;
    *(orbp + 5) = next->regs.entry_ss;
}

static void schedule_context_timer(struct exec_context *next, u64* rsp14, u64* orbp)
{
    /*Your code goes in here. get_current_ctx() still returns the old context*/
    struct exec_context *current = get_current_ctx();
    save_and_restore_timer(current, next, rsp14, orbp);
    printf("scheduling: old pid = %d  new pid  = %d\n", current->pid, next->pid); /*XXX: Don't remove*/
    /*These two lines must be executed*/
    set_tss_stack_ptr(next);
    set_current_ctx(next);
    current->state = READY;
    next->state = RUNNING;
    /*Your code for scheduling context*/
    return;
}

static void save_sleep(struct exec_context* current, struct exec_context* next, u64* dosyscall_rbp)
{
    current->regs.rbx = *(dosyscall_rbp + 15);
    current->regs.rcx = *(dosyscall_rbp + 14);
    current->regs.rdx = *(dosyscall_rbp + 13);
    current->regs.rsi = *(dosyscall_rbp + 12);
    current->regs.rdi = *(dosyscall_rbp + 11);
    current->regs.rbp = *(dosyscall_rbp + 10);
    current->regs.r8 = *(dosyscall_rbp + 9);
    current->regs.r9 = *(dosyscall_rbp + 8);
    current->regs.r10 = *(dosyscall_rbp + 7);
    current->regs.r11 = *(dosyscall_rbp + 6);
    current->regs.r12 = *(dosyscall_rbp + 5);
    current->regs.r13 = *(dosyscall_rbp + 4);
    current->regs.r14 = *(dosyscall_rbp + 3);
    current->regs.r15 = *(dosyscall_rbp + 2);

    current->regs.entry_rip = *(dosyscall_rbp + 16);
    current->regs.entry_cs = *(dosyscall_rbp + 17);
    current->regs.entry_rflags = *(dosyscall_rbp + 18);
    current->regs.entry_rsp = *(dosyscall_rbp + 19);
    current->regs.entry_ss = *(dosyscall_rbp + 20);

    *(dosyscall_rbp + 15) = next->regs.rax;
    *(dosyscall_rbp + 14) = next->regs.rbx;
    *(dosyscall_rbp + 13) = next->regs.rcx;
    *(dosyscall_rbp + 12) = next->regs.rdx;
    *(dosyscall_rbp + 11) = next->regs.rsi;
    *(dosyscall_rbp + 10) = next->regs.rdi;
    *(dosyscall_rbp + 9) = next->regs.rbp;
    *(dosyscall_rbp + 8) = next->regs.r8;
    *(dosyscall_rbp + 7) = next->regs.r9;
    *(dosyscall_rbp + 6) = next->regs.r10;
    *(dosyscall_rbp + 5) = next->regs.r11;
    *(dosyscall_rbp + 4) = next->regs.r12;
    *(dosyscall_rbp + 3) = next->regs.r13;
    *(dosyscall_rbp + 2) = next->regs.r14;
    *(dosyscall_rbp + 1) = next->regs.r15;
    
    *(dosyscall_rbp + 16) = next->regs.entry_rip;
    *(dosyscall_rbp + 17) = next->regs.entry_cs;
    *(dosyscall_rbp + 18) = next->regs.entry_rflags;
    *(dosyscall_rbp + 19) = next->regs.entry_rsp;
    *(dosyscall_rbp + 20) = next->regs.entry_ss;
}

static void schedule_context_sleep(struct exec_context *next, u64* dosyscall_rbp)
{
    /*Your code goes in here. get_current_ctx() still returns the old context*/
    struct exec_context *current = get_current_ctx();
    save_sleep(current, next, dosyscall_rbp);
    printf("scheduling: old pid = %d  new pid  = %d\n", current->pid, next->pid); /*XXX: Don't remove*/
    /*These two lines must be executed*/
    set_tss_stack_ptr(next);
    set_current_ctx(next);
    next->state = RUNNING;
    /*Your code for scheduling context*/
    return;
}

static void pfree_helper()
{
    for(int i = 0; i< 16 ; i++) 
    {
        if(to_free_page[i] > 0x0)
        {
            os_pfn_free(OS_PT_REG, to_free_page[i]);
            to_free_page[i] = 0x0;
        }
    }
}

static struct exec_context *pick_next_context(struct exec_context *list)
{
    /*Your code goes in here*/
    struct exec_context *current = get_current_ctx();
    u32 cpid = current->pid;
    //Check for equality sign in termination condition of loop
    for(int i = cpid+1; i< cpid + MAX_PROCESSES; i++)
    {
        if((i%MAX_PROCESSES) != 0 && list[i%MAX_PROCESSES].state == READY)
            return list + (i%MAX_PROCESSES);
    }

    return list;
}

static void schedule_timer(u64* rsp14, u64* orbp)
{
    struct exec_context *next;
    struct exec_context *current = get_current_ctx();
    struct exec_context *list = get_ctx_list();
    next = pick_next_context(list);
    schedule_context_timer(next, rsp14, orbp);
}

/*The five functions above are just a template. You may change the signatures as you wish*/
void handle_timer_tick()
{
    asm volatile (  "push %%r15;"
                    "push %%r14;"
                    "push %%r13;"
                    "push %%r12;"
                    "push %%r11;"
                    "push %%r10;"
                    "push %%r9;"
                    "push %%r8;"
                    "push %%rdi;"
                    "push %%rsi;"
                    "push %%rdx;"
                    "push %%rcx;"
                    "push %%rbx;"
                    "push %%rax;"
                    ::: "memory"
    );
    u64 *rsp14, *orbp;
    asm volatile (  "mov %%rsp, %0;"
                    :"=r" (rsp14)
                    :
                    :"memory"
    );
    asm volatile (  "mov %%rbp, %0;"
                    :"=r" (orbp)
                    :
                    :"memory"
    );

    /*
    This is the timer interrupt handler.
    You should account timer ticks for alarm and sleep
    and invoke schedule
    */
    pfree_helper();
    printf("Got a tick. #ticks = %u\n", ++numticks);   /*XXX Do not modify this line*/

    u64* urip = (orbp+1);
    u64* ursp = (orbp+4);

    //Update ticks to sleep and ticks t0 alarm
    struct exec_context *current = get_current_ctx();
    struct exec_context *list = get_ctx_list();
    u32 flag = 0;
    for(int i = 0; i < MAX_PROCESSES; i++)
    {
        if(list[i].state == WAITING && list[i].ticks_to_sleep > 0)
        {
            list[i].ticks_to_sleep--;
            if(list[i].ticks_to_sleep==0)
            {
                list[i].state = READY;
            }
        }

        //TODO Confirm wether we have to take running state or not
        if(list[i].alarm_config_time > 0 && list[i].state == RUNNING && list[i].ticks_to_alarm > 0)
        {
            list[i].ticks_to_alarm--;
            if(list[i].ticks_to_alarm==0)
            {
                list[i].ticks_to_alarm = list[i].alarm_config_time;
                invoke_sync_signal(SIGALRM, ursp, urip);
            }
        }

        if(i != 0 && i != current->pid && list[i].state == READY)
        {
            flag = 1;
        }
    }
    if(flag == 1) schedule_timer(rsp14, orbp);
    ack_irq();  /*acknowledge the interrupt, before calling iretq */
    asm volatile (  "mov %0, %%rsp;"
                    "pop %%rax;"
                    "pop %%rbx;"
                    "pop %%rcx;"
                    "pop %%rdx;"
                    "pop %%rsi;"
                    "pop %%rdi;"
                    "pop %%r8;"
                    "pop %%r9;"
                    "pop %%r10;"
                    "pop %%r11;"
                    "pop %%r12;"
                    "pop %%r13;"
                    "pop %%r14;"
                    "pop %%r15;"
                    "mov %%rbp, %%rsp;"
                    "pop %%rbp;"
                    "iretq;"
                    :
                    :"r" (rsp14)
                    :"memory"
    );
}

void do_exit()
{
  /*You may need to invoke the scheduler from here if there are
    other processes except swapper in the system. Make sure you make
    the status of the current process to UNUSED before scheduling
    the next process. If the only process alive in system is swapper,
    invoke do_cleanup() to shutdown gem5 (by crashing it, huh!)
    */
    u64* dosleep_rbp;
    asm volatile(   "mov %%rbp, %0;"
                    :"=r" (dosleep_rbp)
                    :
                    :"memory"
    );
    
    u64* dosyscall_rbp = (u64*)*dosleep_rbp;
    
    struct exec_context *current = get_current_ctx();
    current->state = UNUSED;
    struct exec_context *list = get_ctx_list();
    
    to_free_page[current->pid] = current->os_stack_pfn;

    int flag = 0;
    for(int i = 1; i < MAX_PROCESSES; i++)
    {
        if(list[i].pid !=0 && list[i].state != UNUSED) flag = 1;
    }
    
    if(flag == 0) do_cleanup();  /*Call this conditionally, see comments above*/
    else 
    {
        struct exec_context *next = pick_next_context(list);
        schedule_context_sleep(next, dosyscall_rbp);

        asm volatile (  "mov %0, %%rsp;"
                        "pop %%r15;"
                        "pop %%r14;"
                        "pop %%r13;"
                        "pop %%r12;"
                        "pop %%r11;"
                        "pop %%r10;"
                        "pop %%r9;"
                        "pop %%r8;"
                        "pop %%rbp;"
                        "pop %%rdi;"
                        "pop %%rsi;"
                        "pop %%rdx;"
                        "pop %%rcx;"
                        "pop %%rbx;"
                        "pop %%rax;"
                        "iretq;"
                        :
                        :"r"(dosyscall_rbp+1)
                        :"memory"
        );
    }
}

/*system call handler for sleep*/
long do_sleep(u32 ticks)
{
    struct exec_context *current = get_current_ctx();
    current->ticks_to_sleep = ticks;
    current->state = WAITING;

    u64* dosleep_rbp;
    asm volatile(   "mov %%rbp, %0;"
                    :"=r" (dosleep_rbp)
                    :
                    :"memory"
    );
    
    u64* dosyscall_rbp = (u64*)*dosleep_rbp;
    
    struct exec_context *next;
    struct exec_context *list = get_ctx_list();
    next = pick_next_context(list);
    schedule_context_sleep(next, dosyscall_rbp);

    asm volatile (  "mov %0, %%rsp;"
                    "pop %%r15;"
                    "pop %%r14;"
                    "pop %%r13;"
                    "pop %%r12;"
                    "pop %%r11;"
                    "pop %%r10;"
                    "pop %%r9;"
                    "pop %%r8;"
                    "pop %%rbp;"
                    "pop %%rdi;"
                    "pop %%rsi;"
                    "pop %%rdx;"
                    "pop %%rcx;"
                    "pop %%rbx;"
                    "pop %%rax;"
                    "iretq;"
                    :
                    :"r"(dosyscall_rbp+1)
                    :"memory"
    );
    return ticks;
}

/*
  system call handler for clone, create thread like
  execution contexts
*/
long do_clone(void *th_func, void *user_stack)
{
    u64* dosleep_rbp;
    asm volatile(   "mov %%rbp, %0;"
                    :"=r" (dosleep_rbp)
                    :
                    :"memory"
    );
    
    u64* dosyscall_rbp = (u64*)*dosleep_rbp;
    
    struct exec_context *parent = get_current_ctx();
    struct exec_context *child = get_new_ctx();

    child->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
    child->os_rsp = (u64)omap(child->os_stack_pfn);

    u32 l = strlen(parent->name);
    memcpy(child->name, parent->name, l-1);

    if((child->pid/10) > 0)
    {
        child->name[l-1] = '0' + (child->pid/10);
        child->name[l] = '0' + (child->pid%10);
        l++;
    }
    else 
    {
        child->name[l-1] = '0' + (child->pid);
    }
    child->name[l]=0;

    for(int i=0;i<MAX_SIGNALS;i++)
    {
        child->sighandlers[i] = parent->sighandlers[i];
    }

    for(int i=0;i<MAX_MM_SEGS;i++)
    {
        child->mms[i] = parent->mms[i];
    }

    child->pending_signal_bitmap = 0x0;
    child->used_mem = parent->used_mem;
    child->pgd = parent->pgd;
    child->ticks_to_sleep = 0x0;
    child->ticks_to_alarm = 0x0;
    child->alarm_config_time = 0x0;

    child->regs = parent->regs;

    child->regs.rbp = (u64)user_stack;    
    child->regs.entry_rip = (u64)th_func;
    child->regs.entry_cs = 0x23;
    child->regs.entry_rflags = *(dosyscall_rbp + 18);
    child->regs.entry_rsp = (u64)user_stack;
    child->regs.entry_ss = 0x2b;
    
    child->state = READY;
    
    return child->pid;
}

long invoke_sync_signal(int signo, u64 *ustackp, u64 *urip)
{
    /*If signal handler is registered, manipulate user stack and RIP to execute signal handler*/
    /*ustackp and urip are pointers to user RSP and user RIP in the exception/interrupt stack*/
    printf("Called signal with ustackp=%x urip=%x\n", *ustackp, *urip);
    /*Default behavior is exit( ) if sighandler is not registered for SIGFPE or SIGSEGV.
    Ignore for SIGALRM*/
    
    struct exec_context *current = get_current_ctx();
    u64* sighrip = current->sighandlers[signo];

    if(sighrip == NULL && signo != SIGALRM)
        do_exit();


    if(sighrip != NULL || signo != SIGALRM)
    {
       *ustackp -= 8; //TODO Check what do we have to exactly suvtract 8 or 1

        //Pushing rip to the user stack
        *(u64*)(*ustackp) = *urip;

        *urip = (u64)sighrip;
    }

}

/*system call handler for signal, to register a handler*/
long do_signal(int signo, unsigned long handler)
{
    struct exec_context *current = get_current_ctx();
    u64 oldhandler = (u64)current->sighandlers[signo];
    current->sighandlers[signo] = (u64*)handler;

    return oldhandler;
}

/*system call handler for alarm*/
long do_alarm(u32 ticks)
{
    struct exec_context *current = get_current_ctx();
    u32 oldticks = current->ticks_to_alarm;
    current->ticks_to_alarm = ticks;
    current->alarm_config_time = ticks;

    return oldticks;
}
