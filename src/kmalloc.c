#include "types.h"
#include "stat.h"
#include "param.h"
#include "mmu.h"
#include "memlayout.h"
#include "defs.h"
#include "proc.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

//pretty much a linked list
//ptr points to the next free header
//should be thought of as <__SIZE__|__NEXT_PTR__>
//where each part is 4 bytes long (1 int in size)

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};
extern char data[];  // defined by kernel.ld

typedef union header Header;

static Header kbase; //pointer to beginning of the space
static Header *kfreep; //pointer to the end of the space allocated (all memory past this is unallocated)

int km_allockvm(pde_t *pgdir, uint oldsz, uint newsz);
void* kmalloc(uint nbytes);
void kmfree(void *ap);
static Header* km_morecore(uint nu);
int km_sbrk(int n);
int km_growproc(int n);


void*
kmalloc(uint nbytes)
{
  // cprintf("TEST!");
  Header *p, *prevp;
  uint nunits;
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  //initial case, kfreep == 0... should only happen on initial use
  //set the kbase pointer's s to the location of kbase 
  //also sets the pointer's kfreep and prevp to location of kbase,
  //meaning that their s.ptrs also point to memory location of kbase
  if((prevp = kfreep) == 0){
    kbase.s.ptr = kfreep = prevp = &kbase;
    kbase.s.size = 0;
  }
  //iterates by moving headers from the intial pointer place,
  //then setting p to the pointer of s.ptr, which is equivalent to
  //pointing p to the next header position
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    //if there's no need to allocate more memory (since the current header
    // can fit more data here)
    // cprintf("P: %d %x\n", p->s.size, p->s.ptr);
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr; //perfectly fits, so use this block, removing this current location from the linked list
      else {
        //otherwise break up this header into two parts, one for the nunits size
        //and the other for the remainder, which looks like:
        //<orig_size|orig_nextpointer>[orig_size_in_space] ->
        // <orig_size - nunits|orig_nextpointer>[orig_size - nunits_in_space]&<nunits|.....>[n_units]
        // thus the free block is shortened and the used block is not added to the linked list
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      kfreep = prevp; //kfreep now points to the previous node in linked list 
      // cprintf("About to return %x\n",p + 1 );
      return (void*)(p + 1); //point to the actual data area
    }
    //if the pointers are equivalent (means that all allocated space is filled)
    if(p == kfreep){
      if((p = km_morecore(nunits)) == 0) {
        //request more space for (n units)
        // cprintf("Error creating memory\n");
        return 0;
      }
    }
    
    // cprintf("Finished loop round\n");
  }
  // cprintf("Exiting kmalloc\n");
}

void
kmfree(void *ap)
{
  Header *bp, *p;
  // cprintf("Trying to free memory\n");

  bp = (Header*)ap - 1; // move to see the header info
  //continue until bp is in between p and the next node after p
  //this condition only handles intermediate, the if condition below handles 
  //the condition for bp being at the beginning or end (returns p as the last value in the linked list)
  for(p = kfreep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
  //if p is after the next node in ll and bp is either after p or before the next node
  //meaning that we've traversed to the end (where the next free list value is the beginning of the linked list)
  //and that the to-be free node is either after p (meaning it's the last possible value) or that bp is before both p and the next node
  //meaning that it's before the beginning of the linked list? (need to confirm)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  //thus p either points to the last node (if the linked list's range does not contain bp)
  //or the intermediary node right before bp

  //if the bp chunk is contiguous with the next free block, merge the next block
  //into the bp chunk, otherwise set bp to point to the next value in the linked list
  //should only happen if bp can be placed in an intermediate position in the linked list
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr; 

  //if the free block before bp is contiguous to bp, merge bp into free block,
  //otherwise just add bp the the linked list
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  kfreep = p; //now the kfreep points to node right before bp (if it is within the linked list range), otherwise the last pointer in linked list
  // cprintf("Should be unallocated\n");
}

//generates more memory as needed,
//but is limited 
static Header*
km_morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < PGSIZE - sizeof(Header))
    nu = PGSIZE - sizeof(Header);
  if (nu > PGSIZE - sizeof(Header))
    panic("Kernel cannot allocate more than 4096 bytes per malloc request\n");
  //nu is the size of memory to add (which is at least 1 page (4096 kb))
  //nu * sizeof(header) is the amount of memory added
  // p = (char*) km_sbrk(nu * sizeof(Header)); // returns either -1 or the value of the address of the size needed 
  p = kalloc();
  // cprintf("RETURNED from request %x\n",p);
  if(p == 0) // value for -1 in unsigned
    return 0;
  hp = (Header*)p; //p is the address of the new allocated amount of memory
  hp->s.size = PGSIZE - sizeof(Header);
  kmfree((void*)(hp + 1)); 
  // cprintf("%x\n",kfreep);
  return kfreep;
}