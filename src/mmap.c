#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

void set_memblock(mem_block * block,void *addr, int length, int prot, int flags, int fd, int offset, int used, mem_block * next){
    if (block == 0){
        return;
    }
    block->fd= fd;
    block->length = length;
    block->offset = offset;
    block->region_type = fd== -1? 0: 1;
    block->start_address = addr;
    block->used = used;
    block->next = next;
    block->flags = flags;
    block->prot = prot;
    memset(addr,0,length);
}
void print_memblock(mem_block* block){
    if (block != 0)
    cprintf("Memblock: addr: %x\tlength: %d\tused: %d\tnext: %x\n",
            block->start_address,block->length,block->used,block->next);
}

void print_all_memblocks(mem_block * root){
  mem_block * cur= root;
  while (cur != 0){
    print_memblock(cur);
    cur = cur->next;
  }
}


mem_block * copy_memblock(mem_block * orig){
    mem_block * copy = kmalloc(sizeof(mem_block));
    if (copy ==0){
        return 0;
    }
    copy->start_address = orig->start_address;
    copy->length = orig->length;
    copy->capacity = orig->capacity;
    copy->region_type = orig->region_type;
    copy->offset = orig->offset;
    copy->fd = orig->fd;
    copy->used = orig->used;      
    copy->flags = orig->flags;
    copy->prot = orig->prot; 
    copy->next = orig->next;
    return copy;
}

mem_block * init_memblock(void *addr, int length, int prot, int flags, int fd, int offset){
    mem_block * block = kmalloc(sizeof(mem_block));
    if (block ==0){
        return 0;
    }
    set_memblock(block,addr, length, prot, flags, fd, offset,1,0);
    block->capacity = length;
    return block;
}
mem_block * init_free_memblock(void * addr, int length){
    mem_block * block = kmalloc(sizeof(mem_block));
    if (block ==0){
        return 0;
    }
    set_memblock(block, addr,length,0,0,0,0,0,0);
    block->capacity = length;
    return block;
}

int abs(int x){
    if (x < 0){
        return -x;
    }
    return x;
}

mem_block * lastblock(mem_block * root){
    if (root == 0){
        return 0;
    }
    if (root->next == 0){
        return root;
    }
    return lastblock(root->next);
}

int try_free_lastblock(struct proc * curproc){
    // cprintf("Entering free last block\n");
    uint sz;
    mem_block * last = lastblock(curproc->mapped_mem);
    // print_all_memblocks(curproc->mapped_mem);
    // print_memblock(last);
    if (last == 0){
        return 0;
    }
    // cprintf("HERE\n");
    mem_block * prev =0;
    if (curproc->mapped_mem != last){
      // cprintf("allocating for prev?\n");
      for(prev = curproc->mapped_mem; prev != 0 && prev->next != last; prev = prev->next); 
    }
    if (prev != 0 && prev->next != last){
        cprintf("failed to get node prior to last\n");
    }
    // cprintf("reached here\n");
    //if free mem, should already be unallocated.
    if (last->used == 0 && last->capacity + (uint) last->start_address == curproc->sz){
        // cprintf("Unallocating last proc: ");
        // print_memblock(last);
        sz = (uint) last->start_address;
        if (prev == 0){
            curproc->mapped_mem = 0;
        }else{
            prev->next = 0;
        }
        kmfree(last);
        curproc->sz = sz;
        switchuvm(curproc);
    }
    // cprintf("Exiting attempt to free last block\n");
    return 0;
}

uint max(uint a, uint b){
    return a >= b ? a : b;
}

uint find_closest_addr(void * addr, uint length, mem_block * root, mem_block ** closest_block, int any_valid, uint cur_sz){
    mem_block *cur = root;
    uint temp_addr;
    int break_while = 0;
    uint closest_addr=0;
    uint aligned_addr =((uint)addr % PGSIZE >= PGSIZE/2 ? PGROUNDUP((uint)addr) : PGROUNDDOWN((uint) addr));
    
    while(cur != (mem_block *) 0){
        if (cur->used == 0 && cur->length >= length){
            if (any_valid){
                //just find the first suitable one...
                *closest_block = cur;
                closest_addr = (uint) cur->start_address;
                break;
            }
            if (closest_block == 0){ //for initialization purposes
                *closest_block = cur;
                closest_addr = (uint) cur->start_address;
            }
            //check within each block the optimal position for the position
            //assuming that cur->start_address will always be page aligned
            for(temp_addr = (uint) cur->start_address; temp_addr + length <=  (uint) cur->start_address + cur->length; temp_addr += PGSIZE){
                //if the temp addr is further away than the best addr so far, there's no need to continue since
                //that implies the requested address is behind
                if ((uint)temp_addr % PGSIZE!= 0){
                    cprintf("Somehow not aligned for temp_addr!\n");
                }
                if (abs(temp_addr - aligned_addr) > abs(closest_addr - aligned_addr)){
                    break_while = 1;
                    break;
                }   
                closest_addr = temp_addr;
                *closest_block = cur;
            }
            if (break_while){
                break;
            }
        }
        cur = cur->next;
    }
    if (*closest_block == 0 || abs(PGROUNDUP(cur_sz) - (uint)aligned_addr) < abs((uint) closest_addr - (uint)aligned_addr)){
        //will allocate on top...
        closest_addr =  max(PGROUNDUP(cur_sz), (uint) aligned_addr);
        *closest_block = 0;
    }
    // cprintf("returning closest_addr: %d\n",closest_addr);
    return (uint) closest_addr; 
}

void free_mapped_mem(mem_block * node){

    if (node == 0){
        return;
    }
    if (node->used){
        cprintf("somehow node was still being mapped?\n");
    }
    free_mapped_mem(node->next);
    kmfree(node);
    // cprintf("Is this the issue?\n");
}

//linkedlist must always be sorted 
void * mmap(void *addr, int length, int prot, int flags, int fd, int offset){
    // return 0;
    struct proc * curproc = myproc();
    try_free_lastblock(curproc);//in case there's any new blocks to free

    mem_block * last = lastblock(curproc->mapped_mem); //last block 
    uint sz = curproc->sz;
    uint rounded_sz = PGROUNDUP(sz);
    int any_valid = addr == 0 ? 1 : 0; // used to check if any thing will do
    

    if ((int) addr < 0 || (int) addr >= KERNBASE || length <=0)
    {
        return (void *) -1;
    }
    mem_block * closest_block = 0;    //used for identifing the closest block
    uint closest_addr = find_closest_addr(addr,length,curproc->mapped_mem,&closest_block, any_valid, sz);   //used to break up blocks if necessary

    //if need to allocate more memory
    if (closest_addr >= rounded_sz){
        uint freespace = closest_addr - rounded_sz;
        uint prev_sz = sz;
        if ((sz = allocuvm(curproc->pgdir,curproc->sz, closest_addr + length))==0){
            return (void *) -1;
        }
        if (freespace % PGSIZE != 0){
            cprintf("SHOULD BE AN ERROR (the data should always be page-aligned)!\n");
        }
        if ((closest_block = init_memblock((void *) closest_addr, length, prot, flags, fd, offset))==0){
            deallocuvm(curproc->pgdir,closest_addr + length, prev_sz);
            return (void *) -1;
        }
        mem_block * temp = closest_block;
        if (freespace != 0){
            mem_block * freeblock = init_free_memblock((void *) prev_sz,freespace);
            
            if (freeblock == 0){
                kmfree(closest_block);
                deallocuvm(curproc->pgdir,closest_addr + length, prev_sz);
                return (void *) -1;
            }
            deallocuvm(curproc->pgdir,prev_sz + freespace, prev_sz); //deallocate free block
            freeblock->next = closest_block;
            temp = freeblock;
        }
        if (last == 0){
            curproc->mapped_mem = temp;
        }else{
            last->next = temp;
        }
    }else{
        if (closest_block == 0){
            cprintf("ERROR: SHOULD HAVE FOUND A VALID BLOCK\n");
        }
        //reuse previous freed memory
        int old_len = closest_block->length;
        mem_block * old_nex = closest_block->next;
        uint block_end = PGROUNDUP((uint)closest_addr + length);
        int pre_space = closest_addr - (uint)closest_block->start_address; // guaranteed to be aligned by PGSIZE
        int post_space = ((uint) closest_block->start_address + closest_block->length) - block_end;
        int capacity = block_end - (uint) closest_addr; // guaranteed to be aligned by PGSIZE
        mem_block * inner, *post, * prev;
        inner = post = 0;
        prev = closest_block;

        if (pre_space % PGSIZE != 0 ){
            // cprintf("ERROR WITH either cur_best_addr or the mem blocks: %x %x %x %x\n",closest_addr,length, closest_block->start_address, closest_block->length);
            return (void *) -1;
        }
        if (post_space > 0){
            post = init_free_memblock((void *)block_end,post_space);
            if (post == 0){
                return (void *) -1;
            }
            post->next = closest_block->next; 
            closest_block->length = length;
            closest_block->capacity = capacity;
            closest_block->next = post;
            try_free_lastblock(curproc);//check to see if post can be freed completely (will only happen if post is the last block)
        }
        if (pre_space > 0){
            inner = init_memblock((void *)closest_addr,length,prot,flags,fd, offset);
            if (inner == 0){
                if (post != 0){
                    kmfree(post);
                    closest_block->length = old_len;
                    closest_block->next = old_nex;
                }
                return (void *) -1;
            }
            closest_block->length = pre_space;
            closest_block->capacity = pre_space; 
            inner->capacity = capacity;
            inner->next = closest_block->next; 
            closest_block->next = inner;
            closest_block = inner;
        }
        if (allocuvm(curproc->pgdir,(uint) closest_block->start_address,(uint) closest_block->start_address + closest_block->length)==0){
            if (inner != 0){
                closest_block = prev;
                closest_block->next = inner->next;
                closest_block->capacity += capacity;
                closest_block->length = closest_block->capacity;
                kmfree(inner);
            }
            if (post != 0){
                closest_block->next = post->next;
                closest_block->capacity += post_space;
                closest_block->length = closest_block->capacity;
                kmfree(post);
            }
            // cprintf("Error trying to break up memory...\n");
            return (void *) -1;
        }
        set_memblock(closest_block,closest_block->start_address,closest_block->length,prot,flags,fd,offset,1,closest_block->next);
    }
    curproc->sz = sz;
    switchuvm(curproc);
    print_all_memblocks(curproc->mapped_mem);
    return (void *) closest_block->start_address;
}
int munmap(void *addr, uint length){
    if ((int) addr < 0 || (int) addr >= KERNBASE || length <=0)
    {
        return -1;
    }
    
    struct proc * curproc = myproc();
    mem_block * root = curproc->mapped_mem;
    mem_block * cur, *prev;
    uint sz = curproc->sz;

    // cprintf("Unmapping\n");

    prev = 0;
    for(cur = root; cur != 0 && !(cur->start_address == addr && cur->length == length); prev = cur, cur = cur->next);
    if( cur == 0){
        return -1;
    }
    cur->length = cur->capacity;
    //free from the top of the stack if it's last   
    set_memblock(cur,cur->start_address, cur->length,0,0,0,0,0,cur->next);
    // cprintf("Finished setting mem...");
    sz = deallocuvm(curproc->pgdir,(uint) cur->start_address + cur->length,(uint) cur->start_address);
    if (sz == 0){
        return -1;
    }
    // cprintf("finished deallocating\n");
    //merge with previous node
    if (prev != 0 && prev->used == 0 && prev->start_address + prev->capacity == cur->start_address){
        // cprintf("merging with previous node\n");
        int capacity = cur->capacity;
        prev->length += capacity;
        prev->capacity += capacity;
        prev->next = cur->next;
        kmfree(cur);
        cur = prev;
        for(prev = root; prev != 0 && prev->next != cur; prev = prev->next); // set prev to before cur
    }
    //merge with next node
    if (cur->next != 0 && cur->next->used == 0 &&
        cur->capacity + cur->start_address == cur->next->start_address){
        // cprintf("merging with next node\n");
        mem_block * next = cur->next;
        int capacity = next->capacity;
        cur->length += capacity;
        cur->capacity += capacity;
        cur->next = next->next;
        kmfree(next);
    }    
    //only truly release memory if it's available
    try_free_lastblock(curproc);
    return 0;
}


mem_block * copy_mmap(struct mappedmem * root){
    if (root == 0){
        return 0;
    }
    mem_block * block = copy_memblock(root);
    block->next = copy_mmap(root->next);
    return block;
}