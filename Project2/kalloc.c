// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#define NPID 40000

int kinit_flag = 0;
extern int PID[];
extern uint VPN[];
extern uint PPN[];
extern pte_t PTE_XV6[];

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  kinit_flag = 1;
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  struct run *r;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE){
    if (vend != 0) {
//		memset(p, 1, PGSIZE);
		if(kmem.use_lock)
			acquire(&kmem.lock);
		r = (struct run*)p;
		r->next = kmem.freelist;
		kmem.freelist = r;
		if(kmem.use_lock)
			release(&kmem.lock);
	}
	if (vend == P2V(PHYSTOP)) PID[(int)(V2P(p))/4096] = -1; //If PID[i] is -1, the physical frame i is freespace.
  }
}

uint hash(int pid, char *vpn) {
  //TODO: Implement your own hash function
    uint hash = pid;
    hash ^= ((uint)vpn >> 3) * 31;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash % NPID;
    return hash;
}

void kfree(int pid, char *v){ // v is virtual address

  uint kv, idx;
  struct run *r;
  //TODO: Fill the code that supports kfree
  //1. Find the corresponding physical address for given pid and VA
  //To do 1), find the corresponding entry of inverted page table and read the PPN[idx]
  idx = hash(pid, v); // (added)
  while (PID[idx] != pid || VPN[idx] != (uint)v) {
    idx = (idx + 1) % 40000; // Linear probing to handle collisions // (added)
    if (idx == hash(pid, v)) { // Infinite loop check //debugging
      cprintf("Error: Infinite loop in kfree due to collision.\n");
      return;
    }
  }
 
  //2. Initialize the PID[idx], VPN[idx], PPN[idx] and PTE_XV6[idx]
  PID[idx] = -1; // (added)
  VPN[idx] = 0; // (added)
  PPN[idx] = 0; // (added)
  PTE_XV6[idx] = 0; // (added)
  kv = PPN[idx]; // (added)
  //3. For memset(), convert the physical address for free to kernel's virtual address by using P2V macro
  memset(P2V(kv), 1, PGSIZE); // (added)
  //4. Insert the free page into kmem.freelist
  // memset(kv, 1, PGSIZE); //TODO: You must perform memset for P2V(physical address);
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)P2V(kv); // (modified)
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);

  cprintf("kfree: Freed memory for PID %d, VA %x\n", pid, v); //debugging
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

//Original kfree() code. Refer to this code for implementing inverted-page-table version's kfree()

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
/*
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}
*/

char*
kalloc(int pid, char *v)  // v is virtual address
{

  int idx;
  struct run *r; 

// Do not modify this code
  if (kinit_flag == 0) {
	  if(kmem.use_lock)
		acquire(&kmem.lock);
	r = kmem.freelist;
	if(r)
		kmem.freelist = r->next;
	if(kmem.use_lock)
		release(&kmem.lock);
	return (char*)r;
  }
  
//[PJ2] Implementation Start
//Get free page from kmem.freelist
//if there is no free page in kmem.freelist, return NULL. On current implementation, (char*)r is NULL in that case.

  if(kmem.use_lock)
    acquire(&kmem.lock);

  r = kmem.freelist;
  //if (r) kmem.freelist = r->next;
  if(!r) {
	  if (kmem.use_lock) release(&kmem.lock);
	  cprintf("No free memory\n");
	  return 0;
  }
  kmem.freelist = r->next;
  

  //TODO: Fill the code that supports kalloc
  
  //1. Find the empty entry of inverted page table (PID[idx] is 1) using hash function and linear probing. 
  //When the entry which corresponds an index by hash function is already filled, perform linear probing!
  
  //2. Consider the case that v is -1, which means that the caller of kalloc is kernel
  //so the virtual address is decided by the allocated physical address (P2V) 
  
  //3. Update the value of PID[idx], VPN[idx] and PPN[idx] (Do not update the PTE_XV6[idx] in this code!)
  //4. Return (char*)(*r), if there is no free space, return 0
  idx = hash(pid, v); // (added)
  //cprintf("\n");
  
  while (PID[idx] != -1) {
    cprintf("Hash collision for idx %d: PID %d, VA %x\n", idx, PID[idx], VPN[idx]); //(added) Debugging
    idx = (idx + 1) % 40000; // Linear probing to handle collisions // (added)
  }

  if (v == (char*)-1) { // Kernel caller case // (added)
    v = P2V(V2P(r)); }

  PID[idx] = pid; // (added)
  VPN[idx] = (uint)v; // (added)
  PPN[idx] = V2P(r); // (added)

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r; 
}

