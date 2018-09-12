#include<init.h>
#include<memory.h>
static void exit(int);
static int main(void);


void init_start() {
  int retval = main();
  exit(0);
}

/*Invoke system call with no additional arguments*/
static u64 _syscall0(int syscall_num) {
  asm volatile ( "int $0x80;"
                 "leaveq;"
                 "retq;"
                :::"memory");
  return 0;   /*gcc shutup!*/
}

/*Invoke system call with one argument*/
static u64 _syscall1(int syscall_num, int exit_code) {
  asm volatile ( "int $0x80;"
                 "leaveq;"
                 "retq;"
                :::"memory");
  return 0;   /*gcc shutup!*/
}

/*Invoke system call with two arguments*/
static u64 _syscall2(int syscall_num, u64 param1, u64 param2) {
  asm volatile ( "int $0x80;"
                 "leaveq;"
                 "retq;"
                :::"memory");
  return 0;   /*gcc shutup!*/
}

static int getpid() {
  return(_syscall0(SYSCALL_GETPID));
}

static void exit(int code) {
  _syscall1(SYSCALL_EXIT, code);
}

static int write(char* buf, int length) {
  return(_syscall2(SYSCALL_WRITE, buf, length));
}

static u64 expand(u32 size, int flags) {
  return(_syscall2(SYSCALL_EXPAND, size, flags));
}

static u64 shrink(u32 size, int flags) {
  return(_syscall2(SYSCALL_SHRINK, size, flags));
}

static int main()
{
  // char* ptr = ( char* ) expand(8, MAP_WR);
  // *(ptr + 4096) = 'X';
  // // int x=write(ptr+4096,1);
  // char *ptr2=shrink(8, MAP_WR);
  // *(ptr + 4096) = 'A';
  // int tt=write(ptr+4096,1);
  // // *(ptr + 8192) = 'A';
  // exit(0);

  // u64 ptr = expand(8, MAP_WR);
  // ptr = shrink(ptr, MAP_RD);
  // ptr = shrink(100, MAP_WR);
  // ptr = expand(ptr, MAP_RD);
  // char str[]="I have been written\n";
  // u64 return_size = write(str, 27);
  // u64 return_size2 = expand(return_size, MAP_RD);

  //
  // int x=0;
  // int y=3;
  // int z;

  // void *ptr1;
  // u64 = (u64) expand(8, MAP_WR);
  // char *uless=(char *) shrink(ptr,MAP_RD)

  // void *ptr1;
  // char *ptr;
  // char *ptr1;
  //
  // ptr = (char *) expand(8, MAP_WR);
  //
  //
  // if(ptr == NULL)
  //             write("FAILED\n", 7);
  //
  //
  // // z=y/x;
  // *(ptr + (8192)) = 'A';   //Page fault will occur and handled successfully
  //
  //
  // ptr1 = (char *) shrink(7, MAP_WR);
  // *ptr = 'A';          /*Page fault will occur and handled successfully*/
  //
  // ptr = 0x7ffff0000;
  // char c=*(ptr+(8192));
  // *(ptr+(8192))='a';
  // ptr1 = (char *) shrink(7, MAP_RD);
  // ptr=0x900000000;
  // char *str=0x900000000;
  // u64 return_size=write(str, 14);
  // c=*(ptr);
  // z=y/x;



  //  char* ptr1 = (char*) expand(4,MAP_WR);
  // *(ptr + 4096) = 'V';
  // write(ptr + 4096,1);

  write("Hritvik",10);
  void *ptr1;
  char *ptr = (char *) expand(8, MAP_WR);

  if(ptr == NULL)
              write("FAILED\n", 7);

  *(ptr + 8192) = 'A';   /*Page fault will occur and handled successfully*/
  *(ptr + 8193) = 'A';
  write(ptr+8192,2);
  ptr1 = (char *) shrink(7, MAP_WR);
  *ptr = 'A';          /*Page fault will occur and handled successfully*/

  *(ptr + 4096) = 'A';   /*Page fault will occur and PF handler should termminate
                   the process (gemOS shell should be back) by printing an error message*/
  exit(0);

}


// static int main()
// {
//   unsigned long i;
// #if 0
//   unsigned long *ptr = (unsigned long *)0x100032;
//   i = *ptr;
// #endif
//   i = getpid();
//   exit(-5);
//   return 0;
// }
