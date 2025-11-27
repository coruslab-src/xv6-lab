//
// tests for copy-on-write fork() assignment.
//

#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define N (8 * (1 << 20))

// allocate more than half of physical memory,
// then fork. this will fail in the default
// kernel, which does not support copy-on-write.
void
simpletest()
{
  uint64 phys_size = PHYSTOP - KERNBASE;
  int sz = (phys_size / 3) * 2;

  printf("simple: ");
  
  char *p = sbrk(sz);
  if(p == (char*)0xffffffffffffffffL){
    printf("sbrk(%d) failed\n", sz);
    exit(-1);
  }

  for(char *q = p; q < p + sz; q += 4096){
    *(int*)q = getpid();
  }

  int pid = fork();
  if(pid < 0){
    printf("fork() failed\n");
    exit(-1);
  }

  if(pid == 0)
    exit(0);

  wait(0);

  if(sbrk(-sz) == (char*)0xffffffffffffffffL){
    printf("sbrk(-%d) failed\n", sz);
    exit(-1);
  }

  printf("ok\n");
}

// three processes all write COW memory.
// this causes more than half of physical memory
// to be allocated, so it also checks whether
// copied pages are freed.
void
threetest()
{
  uint64 phys_size = PHYSTOP - KERNBASE;
  int sz = phys_size / 4;
  int pid1, pid2;

  printf("three: ");
  
  char *p = sbrk(sz);
  if(p == (char*)0xffffffffffffffffL){
    printf("sbrk(%d) failed\n", sz);
    exit(-1);
  }

  pid1 = fork();
  if(pid1 < 0){
    printf("fork failed\n");
    exit(-1);
  }
  if(pid1 == 0){
    pid2 = fork();
    if(pid2 < 0){
      printf("fork failed");
      exit(-1);
    }
    if(pid2 == 0){
      for(char *q = p; q < p + (sz/5)*4; q += 4096){
        *(int*)q = getpid();
      }
      for(char *q = p; q < p + (sz/5)*4; q += 4096){
        if(*(int*)q != getpid()){
          printf("wrong content\n");
          exit(-1);
        }
      }
      exit(-1);
    }
    for(char *q = p; q < p + (sz/2); q += 4096){
      *(int*)q = 9999;
    }
    exit(0);
  }

  for(char *q = p; q < p + sz; q += 4096){
    *(int*)q = getpid();
  }

  wait(0);

  sleep(1);

  for(char *q = p; q < p + sz; q += 4096){
    if(*(int*)q != getpid()){
      printf("wrong content\n");
      exit(-1);
    }
  }

  if(sbrk(-sz) == (char*)0xffffffffffffffffL){
    printf("sbrk(-%d) failed\n", sz);
    exit(-1);
  }

  printf("ok\n");
}

char junk1[4096];
int fds[2];
char junk2[4096];
char buf[4096];
char junk3[4096];

// test whether copyout() simulates COW faults.
void
filetest()
{
  printf("file: ");
  
  buf[0] = 99;

  for(int i = 0; i < 4; i++){
    if(pipe(fds) != 0){
      printf("pipe() failed\n");
      exit(-1);
    }
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(-1);
    }
    if(pid == 0){
      sleep(1);
      if(read(fds[0], buf, sizeof(i)) != sizeof(i)){
        printf("error: read failed\n");
        exit(1);
      }
      sleep(1);
      int j = *(int*)buf;
      if(j != i){
        printf("error: read the wrong value\n");
        exit(1);
      }
      exit(0);
    }
    if(write(fds[1], &i, sizeof(i)) != sizeof(i)){
      printf("error: write failed\n");
      exit(-1);
    }
  }

  int xstatus = 0;
  for(int i = 0; i < 4; i++) {
    wait(&xstatus);
    if(xstatus != 0) {
      exit(1);
    }
  }

  if(buf[0] != 99){
    printf("error: child overwrote parent\n");
    exit(1);
  }

  printf("ok\n");
}

//
// try to expose races in page reference counting.
//
void
forkforktest()
{
  printf("forkfork: ");

  int sz = 256 * 4096;
  char *p = sbrk(sz);
  memset(p, 27, sz);

  int children = 3;

  for(int iter = 0; iter < 100; iter++){
    for(int nc = 0; nc < children; nc++){
      if(fork() == 0){
        sleep(2);
        fork();
        fork();
        exit(0);
      }
    }

    for(int nc = 0; nc < children; nc++){
      int st;
      wait(&st);
    }
  }

  sleep(5);
  for(int i = 0; i < sz; i += 4096){
    if(p[i] != 27){
      printf("error: parent's memory was modified!\n");
      exit(1);
    }
  }

  printf("ok\n");
}

void
supercheck(uint64 s)
{
  pte_t last_pte = 0;

  for (uint64 p = s;  p < s + 512 * PGSIZE; p += PGSIZE) {
    pte_t pte = (pte_t) pgpte((void *) p);
    if(pte == 0){
      printf("error: no pte\n");
    exit(1);
      }
    if ((uint64) last_pte != 0 && pte != last_pte) {
        printf("error: pte different\n");
    exit(1);
    }
    // if((pte & PTE_V) == 0 || (pte & PTE_R) == 0 || (pte & PTE_W) == 0){
    //   printf("error pte wrong\n");
    // exit(1);
    // }
    last_pte = pte;
  }

  for(int i = 0; i < 512; i += PGSIZE){
    *(int*)(s+i) = i;
  }

  for(int i = 0; i < 512; i += PGSIZE){
    if(*(int*)(s+i) != i){
      printf("error: wrong value\n");
    exit(1);
      }
  }
}
void
superpg_test()
{
  int pid;
  
  printf("superpg: ");
  
  char *end = sbrk(N);
  if (end == 0 || end == (char*)0xffffffffffffffff){
    printf("error: sbrk failed\n");
    exit(1);
  }
  
  uint64 s = SUPERPGROUNDUP((uint64) end);
  supercheck(s);
  if((pid = fork()) < 0) {
    printf("error: fork failed\n");
    exit(1);
  } else if(pid == 0) {
    supercheck(s);
    exit(0);
  } else {
    int status;
    wait(&status);
    if (status != 0) {
      exit(0);
    }
  }
  printf("ok\n");  
}

int
main(int argc, char *argv[])
{
  simpletest();

  // check that the first simpletest() freed the physical memory.
  simpletest();

  threetest();
  threetest();
  threetest();

  filetest();

  forkforktest();

  superpg_test();

  printf("ALL COW TESTS PASSED\n");

  exit(0);
}
