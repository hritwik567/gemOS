#include<context.h>
#include<memory.h>
#include<lib.h>

#define OFFSET_MASK 0x0000000000000fff
#define L1_MASK     0x00000000001ff000
#define L2_MASK     0x000000003fe00000
#define L3_MASK     0x0000007fc0000000
#define L4_MASK     0x0000ff8000000000
#define L1_SHIFT    12
#define L2_SHIFT    21
#define L3_SHIFT    30
#define L4_SHIFT    39
#define SHIFT       12
#define IMD_MASK    0x5
#define omap(u)     (u64*)osmap(u)

void cleanup(u32 pfn){
  for(u32 i=0;i<512;i++){
    *(omap(pfn)+i) = 0x0;
  }
}

void prepare_context_mm(struct exec_context *ctx){
  //page table pages
  u32 os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);

  //physical frame number of the first level translation
  ctx->pgd = os_ptp;

  //code and stack page
  u32 user_code_pfn = os_pfn_alloc(USER_REG);
  u32 user_stack_pfn = os_pfn_alloc(USER_REG);
  u32 user_data_pfn = ctx->arg_pfn;

  //access flags
  u32 caf = (ctx->mms[MM_SEG_CODE].access_flags >> 1) & 1;
  u32 saf = (ctx->mms[MM_SEG_STACK].access_flags >> 1) & 1;
  u32 daf = (ctx->mms[MM_SEG_DATA].access_flags >> 1) & 1;

  //start addresses
  u64 csa = ctx->mms[MM_SEG_CODE].start;
  u64 dsa = ctx->mms[MM_SEG_DATA].start;
  u64 ssa = ctx->mms[MM_SEG_STACK].end-1;
  // printf("code start %x, access_flags %x %x\n",csa, ctx->mms[MM_SEG_CODE].access_flags, caf);
  // printf("data start %x, access_flags %x %x\n",dsa, ctx->mms[MM_SEG_DATA].access_flags, saf);
  // printf("stack start %x, access_flags %x %x\n",ssa, ctx->mms[MM_SEG_STACK].access_flags, daf);
  //populating code block
  u32 os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
  u64 l = (csa & L4_MASK) >> L4_SHIFT;
  *(omap(ctx->pgd)+l) = (u64)(os_ptp_temp << SHIFT) + (IMD_MASK | caf<<1);
  // printf("code l4 %x\n",l);
  // printf("code val l4 %x\n",ctx->pgd);
  // printf("code PTE l4 %x\n",*(omap(ctx->pgd)+l));

  os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);
  l = (csa & L3_MASK) >> L3_SHIFT;
  *(omap(os_ptp_temp)+l) = (u64)(os_ptp << SHIFT) | (IMD_MASK | caf<<1);
  // printf("code l3 %x\n",l);
  // printf("code val l3 %x\n",os_ptp_temp);
  // printf("code PTE l3 %x\n",*(omap(os_ptp_temp)+l));

  os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
  l = (csa & L2_MASK) >> L2_SHIFT;
  *(omap(os_ptp)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | caf<<1);
  // printf("code l2 %x\n",l);
  // printf("code val l2 %x\n",os_ptp);
  // printf("code PTE l2 %x\n",*(omap(os_ptp)+l));
  // printf("%x\n",((ctx->mms[MM_SEG_CODE].access_flags>>1)&1));
  l = (csa & L1_MASK) >> L1_SHIFT;
  *(omap(os_ptp_temp)+l) = (u64)(user_code_pfn << SHIFT) | (IMD_MASK | caf<<1);
  // printf("code l1 %x\n",l);
  // printf("code val l1 %x\n",os_ptp_temp);
  // printf("code PTE l1 %x\n",*(omap(os_ptp_temp)+l));
  // printf("code pfn %x\n\n\n\n",user_code_pfn);

  //populating data block
  l = (dsa & L4_MASK) >> L4_SHIFT;
  *(omap(ctx->pgd)+l) |= daf<<1;
  if((u32)(*(omap(ctx->pgd)+l)&1)==0){
    os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
    *(omap(ctx->pgd)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | daf<<1);
  } else {
    os_ptp_temp = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
  }
  // printf("data l4 %x\n",l);
  // printf("data val l4 %x\n",ctx->pgd);
  // printf("data PTE l4 %x\n",*(omap(ctx->pgd)+l));

  l = (dsa & L3_MASK) >> L3_SHIFT;
  *(omap(os_ptp_temp)+l) |= daf<<1;
  if((u32)((*(omap(os_ptp_temp)+l))&1)==0){
    os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);
    *(omap(os_ptp_temp)+l) = (u64)(os_ptp << SHIFT) | (IMD_MASK | daf<<1);
  } else {
    os_ptp = ((*(omap(os_ptp_temp)+l) << SHIFT) >> SHIFT*2);
  }
  // printf("data l3 %x\n",l);
  // printf("data val l3 %x\n",os_ptp_temp);
  // printf("data PTE l3 %x\n",*(omap(os_ptp_temp)+l));

  l = (dsa & L2_MASK) >> L2_SHIFT;
  *(omap(os_ptp)+l) |= daf<<1;
  if((u32)(*(omap(os_ptp)+l)&1)==0){
    os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
    *(omap(os_ptp)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | daf<<1);
  } else {
    os_ptp_temp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
  }
  // printf("data l2 %x\n",l);
  // printf("data val l2 %x\n",os_ptp);
  // printf("data PTE l2 %x\n",*(omap(os_ptp)+l));

  l = (dsa & L1_MASK) >> L1_SHIFT;
  *(omap(os_ptp_temp)+l) = (u64)(user_data_pfn << SHIFT) | (IMD_MASK | daf<<1);
  // printf("data l1 %x\n",l);
  // printf("data val l1 %x\n",os_ptp_temp);
  // printf("data PTE l1 %x\n",*(omap(os_ptp_temp)+l));
  // printf("data pfn %x\n\n\n\n\n",user_data_pfn);

  //populating stack block
  l = (ssa & L4_MASK) >> L4_SHIFT;
  *(omap(ctx->pgd)+l) |= saf<<1;
  if((u32)(*(omap(ctx->pgd)+l)&1)==0){
    os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
    *(omap(ctx->pgd)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | saf<<1);
  } else {
    os_ptp_temp = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
  }
  // printf("stack l4 %x\n",l);
  // printf("stack val l4 %x\n",ctx->pgd);
  // printf("stack PTE l4 %x\n",*(omap(ctx->pgd)+l));

  l = (ssa & L3_MASK) >> L3_SHIFT;
  *(omap(os_ptp_temp)+l) |= saf<<1;
  if((u32)(*(omap(os_ptp_temp)+l)&1)==0){
    os_ptp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp);
    *(omap(os_ptp_temp)+l) = (u64)(os_ptp << SHIFT) | (IMD_MASK | saf<<1);
  } else {
    os_ptp = ((*(omap(os_ptp_temp)+l) << SHIFT) >> SHIFT*2);
  }
  // printf("stack l3 %x\n",l);
  // printf("stack val l3 %x\n",os_ptp_temp);
  // printf("stack PTE l3 %x\n",*(omap(os_ptp_temp)+l));

  l = (ssa & L2_MASK) >> L2_SHIFT;
  *(omap(os_ptp)+l) |= saf<<1;
  if((u32)(*(omap(os_ptp)+l)&1)==0){
    os_ptp_temp = os_pfn_alloc(OS_PT_REG);cleanup(os_ptp_temp);
    *(omap(os_ptp)+l) = (u64)(os_ptp_temp << SHIFT) | (IMD_MASK | saf<<1);
  } else {
    os_ptp_temp = ((*(omap(os_ptp)+l) << SHIFT) >> SHIFT*2);
  }
  // printf("stack l2 %x\n",l);
  // printf("stack val l2 %x\n",os_ptp);
  // printf("stack PTE l2 %x\n",*(omap(os_ptp)+l));

  l = (ssa & L1_MASK) >> L1_SHIFT;
  *(omap(os_ptp_temp)+l) = (u64)(user_stack_pfn << SHIFT) | (IMD_MASK | saf<<1);
  // printf("stack l1 %x\n",l);
  // printf("stack val l1 %x\n",os_ptp_temp);
  // printf("stack PTE l1 %x\n",*(omap(os_ptp_temp)+l));
  // printf("stack pfn %x\n\n\n\n\n",user_stack_pfn);

  return;
}

u32 notin(u32 p[], u32 p1, u32 n){
  for(u32 i=0;i<n;i++){
      if(p[i]==p1)return 0;
  }
  return 1;
}

void cleanup_context_mm(struct exec_context *ctx){
  //start addresses
  u64 csa = ctx->mms[MM_SEG_CODE].start;
  u64 dsa = ctx->mms[MM_SEG_DATA].start;
  u64 ssa = ctx->mms[MM_SEG_STACK].end-1;
  // printf("code start %x, access_flags %x\n",csa, ctx->mms[MM_SEG_CODE].access_flags);
  // printf("data start %x, access_flags %x\n",dsa,ctx->mms[MM_SEG_DATA].access_flags);
  // printf("stack start %x, access_flags %x\n",ssa,ctx->mms[MM_SEG_STACK].access_flags);

  u32 i = 0;
  u32 p[13];

  u64 l = (csa & L4_MASK) >> L4_SHIFT;
  u32 p1 = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
  l = (dsa & L4_MASK) >> L4_SHIFT;
  u32 p2 = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
  l = (ssa & L4_MASK) >> L4_SHIFT;
  u32 p3 = ((*(omap(ctx->pgd)+l) << SHIFT) >> SHIFT*2);
  os_pfn_free(OS_PT_REG, ctx->pgd);
  p[i++] = ctx->pgd;

  l = (csa & L3_MASK) >> L3_SHIFT;
  u32 pp1 = ((*(omap(p1)+l) << SHIFT) >> SHIFT*2);
  l = (dsa & L3_MASK) >> L3_SHIFT;
  u32 pp2 = ((*(omap(p2)+l) << SHIFT) >> SHIFT*2);
  l = (ssa & L3_MASK) >> L3_SHIFT;
  u32 pp3 = ((*(omap(p3)+l) << SHIFT) >> SHIFT*2);

  if(notin(p,p1,i)){
    os_pfn_free(OS_PT_REG, p1);
    p[i++] = p1;
  }
  if(notin(p,p2,i)){
    os_pfn_free(OS_PT_REG, p2);
    p[i++] = p2;
  }
  if(notin(p,p3,i)){
    os_pfn_free(OS_PT_REG, p3);
    p[i++] = p3;
  }

  l = (csa & L2_MASK) >> L2_SHIFT;
  p1 = ((*(omap(pp1)+l) << SHIFT) >> SHIFT*2);
  l = (dsa & L2_MASK) >> L2_SHIFT;
  p2 = ((*(omap(pp2)+l) << SHIFT) >> SHIFT*2);
  l = (ssa & L2_MASK) >> L2_SHIFT;
  p3 = ((*(omap(pp3)+l) << SHIFT) >> SHIFT*2);

  if(notin(p,pp1,i)){
    os_pfn_free(OS_PT_REG, pp1);
    p[i++] = pp1;
  }
  if(notin(p,pp2,i)){
    os_pfn_free(OS_PT_REG, pp2);
    p[i++] = pp2;
  }
  if(notin(p,pp3,i)){
    os_pfn_free(OS_PT_REG, pp3);
    p[i++] = pp3;
  }

  l = (csa & L1_MASK) >> L1_SHIFT;
  pp1 = ((*(omap(p1)+l) << SHIFT) >> SHIFT*2);
  l = (dsa & L1_MASK) >> L1_SHIFT;
  pp2 = ((*(omap(p2)+l) << SHIFT) >> SHIFT*2);
  l = (ssa & L1_MASK) >> L1_SHIFT;
  pp3 = ((*(omap(p3)+l) << SHIFT) >> SHIFT*2);

  if(notin(p,p1,i)){
    os_pfn_free(OS_PT_REG, p1);
    p[i++] = p1;
  }
  if(notin(p,p2,i)){
    os_pfn_free(OS_PT_REG, p2);
    p[i++] = p2;
  }
  if(notin(p,p3,i)){
    os_pfn_free(OS_PT_REG, p3);
    p[i++] = p3;
  }

  os_pfn_free(USER_REG, pp1);
  os_pfn_free(USER_REG, pp2);
  os_pfn_free(USER_REG, pp3);

  return;
}
