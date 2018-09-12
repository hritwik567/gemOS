#include<init.h>
#include<lib.h>
#include<context.h>
#include<memory.h>

#define L1_MASK     0x00000000001ff000
#define L2_MASK     0x000000003fe00000
#define L3_MASK     0x0000007fc0000000
#define L4_MASK     0x0000ff8000000000
#define L1_SHIFT    12
#define L2_SHIFT    21
#define L3_SHIFT    30
#define L4_SHIFT    39
#define SHIFT       12
#define omap(u)     (u64*)osmap(u)
#define IMD_MASK    5

void cleanup(u32 pfn){
  for(u32 i=0;i<512;i++){
    *(omap(pfn)+i) = 0x0;
  }
}

/*System Call handler*/
u64 do_syscall(int syscall, u64 param1, u64 param2, u64 param3, u64 param4) {
    struct exec_context *current = get_current_ctx();
    printf("[GemOS] System call invoked. syscall no  = %d\n", syscall);
    u64 p, l;
    u32 os_ptp, os_page;
    switch(syscall) {
        case SYSCALL_EXIT:
            printf("[GemOS] exit code = %d\n", (int) param1);
            do_exit();
            break;

        case SYSCALL_GETPID:
            printf("[GemOS] getpid called for process %s, with pid = %d\n", current->name, current->id);
            return current->id;

        case SYSCALL_WRITE:
            //length error
            if(param2 > 1024) return -1;

            //invalid address
            l = (param1 & L4_MASK) >> L4_SHIFT;
            if((u32)(*(omap(current->pgd)+l)&1)==0) return -1;
            os_ptp = ((*(omap(current->pgd)+l) << SHIFT) >> SHIFT*2);
            l = (param1 & L3_MASK) >> L3_SHIFT;
            if((u32)(*(omap(os_ptp)+l)&1)==0) return -1;
            os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
            l = (param1 & L2_MASK) >> L2_SHIFT;
            if((u32)(*(omap(os_ptp)+l)&1)==0) return -1;
            os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
            l = (param1 & L1_MASK) >> L1_SHIFT;
            if((u32)(*(omap(os_ptp)+l)&1)==0) return -1;

            //invalid end address
            l = ((param1 + param2 - 1) & L4_MASK) >> L4_SHIFT;
            if((u32)(*(omap(current->pgd)+l)&1)==0) return -1;
            os_ptp = ((*(omap(current->pgd)+l) << SHIFT) >> SHIFT*2);
            l = ((param1 + param2 - 1) & L3_MASK) >> L3_SHIFT;
            if((u32)(*(omap(os_ptp)+l)&1)==0) return -1;
            os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
            l = ((param1 + param2 - 1) & L2_MASK) >> L2_SHIFT;
            if((u32)(*(omap(os_ptp)+l)&1)==0) return -1;
            os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
            l = ((param1 + param2 - 1) & L1_MASK) >> L1_SHIFT;
            if((u32)(*(omap(os_ptp)+l)&1)==0) return -1;

            for(int i=0;i<param2;i++){
                printf("%c",*((char*)param1 + i));
            }

            return param2;

        case SYSCALL_EXPAND:
            if(param1 > 512) return -1;
            param1 *= PAGE_SIZE;
            if(param2 == MAP_RD) {
                if(param1 > current->mms[MM_SEG_RODATA].end - current->mms[MM_SEG_RODATA].next_free)return 0;
                p = current->mms[MM_SEG_RODATA].next_free;
                current->mms[MM_SEG_RODATA].next_free += param1;
                return p;
            } else if(param2 == MAP_WR) {
                if(param1 > current->mms[MM_SEG_DATA].end - current->mms[MM_SEG_DATA].next_free)return 0;
                p = current->mms[MM_SEG_DATA].next_free;
                current->mms[MM_SEG_DATA].next_free += param1;
                return p;
            }

        case SYSCALL_SHRINK:
            if(param2 == MAP_RD) {
                if(current->mms[MM_SEG_RODATA].next_free - current->mms[MM_SEG_RODATA].start < param1*PAGE_SIZE)return 0;
                for(u64 i=0;i<param1;i++) {
                    current->mms[MM_SEG_RODATA].next_free -= PAGE_SIZE;
                    p = current->mms[MM_SEG_RODATA].next_free;
                    l = (p & L4_MASK) >> L4_SHIFT;
                    if((u32)(*(omap(current->pgd)+l)&1)!=0){
                        os_ptp = ((*(omap(current->pgd)+l) << SHIFT) >> SHIFT*2);
                        l = (p & L3_MASK) >> L3_SHIFT;
                        if((u32)(*(omap(os_ptp)+l)&1)!=0){
                            os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
                            l = (p & L2_MASK) >> L2_SHIFT;
                            if((u32)(*(omap(os_ptp)+l)&1)!=0){
                                os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
                                l = (p & L1_MASK) >> L1_SHIFT;
                                if((u32)(*(omap(os_ptp)+l)&1)!=0){
                                    os_page = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
                                    *(omap(os_ptp)+l) = 0x0;
                                    os_pfn_free(USER_REG, os_page);
                                    asm volatile ("invlpg (%0);" ::"r" (p) :"memory");
                                }
                            }
                        }
                    }
                }
                return p;
            } else if(param2 == MAP_WR) {
                if(current->mms[MM_SEG_DATA].next_free - current->mms[MM_SEG_DATA].start < param1*PAGE_SIZE)return 0;
                for(u64 i=0;i<param1;i++) {
                    current->mms[MM_SEG_DATA].next_free -= PAGE_SIZE;
                    p = current->mms[MM_SEG_DATA].next_free;
                    l = (p & L4_MASK) >> L4_SHIFT;
                    if((u32)(*(omap(current->pgd)+l)&1)!=0){
                        os_ptp = ((*(omap(current->pgd)+l) << SHIFT) >> SHIFT*2);
                        l = (p & L3_MASK) >> L3_SHIFT;
                        if((u32)(*(omap(os_ptp)+l)&1)!=0){
                            os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
                            l = (p & L2_MASK) >> L2_SHIFT;
                            if((u32)(*(omap(os_ptp)+l)&1)!=0){
                                os_ptp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
                                l = (p & L1_MASK) >> L1_SHIFT;
                                if((u32)(*(omap(os_ptp)+l)&1)!=0){
                                    os_page = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
                                    *(omap(os_ptp)+l) = 0x0;
                                    os_pfn_free(USER_REG, os_page);
                                    asm volatile ("invlpg (%0);" ::"r" (p) :"memory");
                                }
                            }
                        }
                    }
                }
                return p;
            }
        default:
            return -1;

    }
    return 0;   /*GCC shut up!*/
}

extern int handle_div_by_zero(void) {
    struct exec_context *current = get_current_ctx();
    u64* rbp;
    asm volatile (  "mov %%rbp, %0;"
                    :"=r" (rbp)
                    :
                    :"memory"
    );
    printf("Div-by-zero detected at [%x]\n", *(rbp+1));
    do_exit();
    return 0;
}

extern void handle_page_fault(void) {
    asm volatile (  "push %%r8;"
                    "push %%r9;"
                    "push %%r10;"
                    "push %%r11;"
                    "push %%r12;"
                    "push %%r13;"
                    "push %%r14;"
                    "push %%r15;"
                    "push %%rax;"
                    "push %%rbx;"
                    "push %%rcx;"
                    "push %%rdx;"
                    "push %%rsi;"
                    "push %%rdi;"
                    ::: "memory"
    );
    u64* rsp14;
    asm volatile (  "mov %%rsp, %0;"
                    :"=r" (rsp14)
                    :
                    :"memory"
    );

    struct exec_context *ctx = get_current_ctx();
    u64 *orbp;
    u64 l, fva;
    u32 os_ptp, os_ptp_temp, af;
    asm volatile (  "mov %%rbp, %0;"
                    :"=r" (orbp)
                    :
                    :"memory"
    );
    asm volatile (  "mov %%cr2, %0;"
                    :"=r" (fva)
                    :
                    :"memory"
    );
    u64 urbp = *(orbp);
    u64 error = *(orbp+1);
    u64 urip = *(orbp+2);

    if((u32)(error & 1)==1){
        printf("*PAGE FAULT* \n RIP [%x] | Accessed Virtual Address [%x] | Error Code [%x]\n", urip, fva, error);
        printf("Protection Fault, Exiting ...\n");
        do_exit();
    }

    if(fva >= ctx->mms[MM_SEG_DATA].start && fva <= ctx->mms[MM_SEG_DATA].end){
        if(fva >= ctx->mms[MM_SEG_DATA].next_free){
            printf("*PAGE FAULT* \n RIP [%x] | Accessed Virtual Address [%x] | Error Code [%x]\n", urip, fva, error);
            printf("Wrong Virtual Address in Data Segment, Exiting ...\n");
            do_exit();
        }
        af = (ctx->mms[MM_SEG_DATA].access_flags >> 1) & 1;

        l = (fva & L4_MASK) >> L4_SHIFT;
        *(omap(ctx->pgd)+l) |= af<<1;
        if((u32)(*(omap(ctx->pgd)+l)&1)==0){
            os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
            *(omap(ctx->pgd)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | af<<1);
        } else {
            os_ptp_temp = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L3_MASK) >> L3_SHIFT;
        *(omap(os_ptp_temp)+l) |= af<<1;
        if((u32)((*(omap(os_ptp_temp)+l))&1)==0){
            os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);
            *(omap(os_ptp_temp)+l) = (u64)(os_ptp << SHIFT) | (IMD_MASK | af<<1);
        } else {
            os_ptp = ((*(omap(os_ptp_temp)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L2_MASK) >> L2_SHIFT;
        *(omap(os_ptp)+l) |= af<<1;
        if((u32)(*(omap(os_ptp)+l)&1)==0){
            os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
            *(omap(os_ptp)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | af<<1);
        } else {
            os_ptp_temp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L1_MASK) >> L1_SHIFT;
        *(omap(os_ptp_temp)+l) = (u64)(os_pfn_alloc(USER_REG) << SHIFT) | (IMD_MASK | af<<1);
    } else if(fva >= ctx->mms[MM_SEG_RODATA].start && fva <= ctx->mms[MM_SEG_RODATA].end){
        if(fva >= ctx->mms[MM_SEG_RODATA].next_free){
            printf("*PAGE FAULT* \n RIP [%x] | Accessed Virtual Address [%x] | Error Code [%x]\n", urip, fva, error);
            printf("Wrong Virtual Address in Read Only Data Segment, Exiting ...\n");
            do_exit();
        }
        if((u64)(error & 2)==2){
            printf("*PAGE FAULT* \n RIP [%x] | Accessed Virtual Address [%x] | Error Code [%x]\n", urip, fva, error);
            printf("Trying to write in Read Only Data Segment, Exiting ...\n");
            do_exit();
        }

        af = (ctx->mms[MM_SEG_RODATA].access_flags >> 1) & 1;

        l = (fva & L4_MASK) >> L4_SHIFT;
        *(omap(ctx->pgd)+l) |= af<<1;
        if((u32)(*(omap(ctx->pgd)+l)&1)==0){
          os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
          *(omap(ctx->pgd)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | af<<1);
        } else {
          os_ptp_temp = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L3_MASK) >> L3_SHIFT;
        *(omap(os_ptp_temp)+l) |= af<<1;
        if((u32)((*(omap(os_ptp_temp)+l))&1)==0){
          os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);
          *(omap(os_ptp_temp)+l) = (u64)(os_ptp << SHIFT) | (IMD_MASK | af<<1);
        } else {
          os_ptp = ((*(omap(os_ptp_temp)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L2_MASK) >> L2_SHIFT;
        *(omap(os_ptp)+l) |= af<<1;
        if((u32)(*(omap(os_ptp)+l)&1)==0){
          os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
          *(omap(os_ptp)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | af<<1);
        } else {
          os_ptp_temp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L1_MASK) >> L1_SHIFT;
        *(omap(os_ptp_temp)+l) = (u64)(os_pfn_alloc(USER_REG) << SHIFT) | (IMD_MASK | af<<1);
    } else if(fva >= ctx->mms[MM_SEG_STACK].start && fva <= ctx->mms[MM_SEG_STACK].end){

        af = (ctx->mms[MM_SEG_STACK].access_flags >> 1) & 1;

        l = (fva & L4_MASK) >> L4_SHIFT;
        *(omap(ctx->pgd)+l) |= af<<1;
        if((u32)(*(omap(ctx->pgd)+l)&1)==0){
          os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
          *(omap(ctx->pgd)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | af<<1);
        } else {
          os_ptp_temp = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L3_MASK) >> L3_SHIFT;
        *(omap(os_ptp_temp)+l) |= af<<1;
        if((u32)((*(omap(os_ptp_temp)+l))&1)==0){
          os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);
          *(omap(os_ptp_temp)+l) = (u64)(os_ptp << SHIFT) | (IMD_MASK | af<<1);
        } else {
          os_ptp = ((*(omap(os_ptp_temp)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L2_MASK) >> L2_SHIFT;
        *(omap(os_ptp)+l) |= af<<1;
        if((u32)(*(omap(os_ptp)+l)&1)==0){
          os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
          *(omap(os_ptp)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | af<<1);
        } else {
          os_ptp_temp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
        }

        l = (fva & L1_MASK) >> L1_SHIFT;
        *(omap(os_ptp_temp)+l) = (u64)(os_pfn_alloc(USER_REG) << SHIFT) | (IMD_MASK | af<<1);

    } else {
        printf("*PAGE FAULT* \n RIP [%x] | Accessed Virtual Address [%x] | Error Code [%x]\n", urip, fva, error);
        printf("Wrong Virtual Memory Segment, Exiting ...\n");
        do_exit();
    }

    asm volatile (  "mov %0, %%rsp;"
                    "pop %%rdi;"
                    "pop %%rsi;"
                    "pop %%rdx;"
                    "pop %%rcx;"
                    "pop %%rbx;"
                    "pop %%rax;"
                    "pop %%r15;"
                    "pop %%r14;"
                    "pop %%r13;"
                    "pop %%r12;"
                    "pop %%r11;"
                    "pop %%r10;"
                    "pop %%r9;"
                    "pop %%r8;"
                    "mov %%rbp, %%rsp;"
                    "pop %%rbp;"
                    "add $8, %%rsp;"
                    "iretq;"
                  :
                  : "r" (rsp14)
                  : "memory"
    );
    return;
}
