#include "paging.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "frame.h"

#include "../threads/malloc.h"
#include "../lib/debug.h"
#include "../threads/pte.h"
#include "../threads/palloc.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"
#include "../userprog/pagedir.h"
#include "../userprog/syscall.h"
#include "swap.h"
#include "../filesys/filesys.h"
#include "../lib/string.h"
#include "../userprog/files.h"


static bool supp_pagedir_really_create(struct supp_pagedir_entry *el);
static void paging_activate_no_lock(struct supp_pagedir_entry *f);
/**
 * init supp pagedir internal structures
 * @return pointer to the supp pagedir struct
 */
struct supp_pagedir* supp_pagedir_init(void){
  struct supp_pagedir * ret = calloc(1, sizeof(struct supp_pagedir));
  ASSERT(ret);
  lock_init(&ret->lock);
  return ret;
}


void supp_pagedir_set_readfile(void *vaddr, int fd, int s, int e, int flags){
  struct supp_pagedir_entry ** ee = supp_pagedir_lookup(thread_current()->supp_pagedir, vaddr, false);
  ASSERT(ee);
  struct supp_pagedir_entry *el=*ee;
  ASSERT(el->sector_t == -1);
  ASSERT(el);
  el->flags |= PAL_ZERO | flags;
  ASSERT(ee);
  el=*ee;
  ASSERT(el);

  el->fd = fd;
  el->s = s;
  el->e = e;
}

/**
 * Looks up for the mapping from upage to supp pagedir entry.
 * @param table  supp pagedir
 * @param upage
 * @param create should create if not exists
 * @return pointer to the pointer(of the entry).
 */
struct supp_pagedir_entry **
supp_pagedir_lookup (struct supp_pagedir *table, const void *upage, bool create)
{
  ASSERT (table);
  ASSERT(upage);
  ASSERT(pd_no(upage) < (1<<PDBITS));
  struct supp_pagedir2 *pde = table->entries[pd_no(upage)];
  if(pde == NULL){
    if(create) {
      pde = table->entries[pd_no(upage)] = calloc(1, sizeof(struct supp_pagedir2));
      ASSERT(pde);
    }else {
      return NULL;
    }
  }
  ASSERT(pt_no(upage) < (1<<PTBITS));

  /* Return the page table entry. */
  if(pde->entries[pt_no(upage)] != NULL) {
    ASSERT(pde->entries[pt_no(upage)]->MAGIC == PAGING_MAGIC);
  }
  struct supp_pagedir_entry **ret =  &pde->entries[pt_no (upage)];
  return ret;
}


void paging_activate(struct supp_pagedir_entry *f){
  lock_acquire(&f->lock);
  paging_activate_no_lock(f);
  lock_release(&f->lock);
}
/**
 * Makes page real. If it's not mapped to the palloc, mapps it to one. Also reocvers data if any.
 * @param f
 */
static void paging_activate_no_lock(struct supp_pagedir_entry *f){
  void *upage = f->upage;
  if(!pagedir_get_page(thread_current()->pagedir, f->upage))
    supp_pagedir_really_create(f);

  void *kpage = pagedir_get_page(thread_current()->pagedir, f->upage);
  ASSERT(kpage);

  if(f->sector_t != BLOCK_SECTOR_T_ERROR){
    swap_read(f->sector_t, f->upage);
    f->sector_t = BLOCK_SECTOR_T_ERROR;
  }else if(f->fd != -1){
    int fd = f->fd;
    f->fd = -1;

    ASSERT(upage == pg_round_down(upage));
    seek_sys(fd, f->s);
    ASSERT(f->e - f->s <= PGSIZE);
    int read_size = f->e - f->s;
    read_sys(fd, kpage, read_size);
    memset(kpage + read_size, 0, PGSIZE - read_size);
    f->fd = fd; // I need it activate not to be recalled for many times
  }

}

/**
 * initializes upage in supplemental page table, but does not acquire any frame.
 * @param upage virtual user address
 * @param flag flags
 */
void supp_pagedir_virtual_create(void *upage, enum palloc_flags flag){
  ASSERT(pg_round_down(upage) == upage);
  struct supp_pagedir *table = thread_current()->supp_pagedir;
  ASSERT(table);
  ASSERT(upage);
  ASSERT(pg_ofs (upage) == 0);
  ASSERT(is_user_vaddr (upage));
  ASSERT(flag & PAL_USER);

  struct supp_pagedir_entry ** elem = supp_pagedir_lookup(table, upage, true);
  if(elem == NULL || *elem != NULL){
    PANIC("Upage %u is already used. elem = %u, *elem = %u",
          (uint32_t)upage,  (uint32_t)elem,  (uint32_t)((elem == NULL)?NULL:*elem));
  } else{
    struct supp_pagedir_entry * el = *elem = malloc(sizeof(struct supp_pagedir_entry));

    ASSERT(table->entries[pd_no(upage)]);
    ASSERT(table->entries[pd_no(upage)]->entries[pt_no(upage)]);
    el->MAGIC = PAGING_MAGIC;
    el->flags = flag;
    el->pagedir = &thread_current()->pagedir;
    el->sector_t = BLOCK_SECTOR_T_ERROR;
    el->upage = upage;
    el->s = el->e = el->fd = -1;
    lock_init(&el->lock);
  }

}

/**
 * acquires frame for given user virtual adddres upage.
 * mapping should not exist in pagedir(if it does then frame is already linked to it)
 * @param upage
 * @return
 */
static bool supp_pagedir_really_create(struct supp_pagedir_entry *el){
  uint32_t *pd = thread_current()->pagedir;
  void *upage = el->upage;
  void *kpage = frame_get_page(el->flags, el);

  ASSERT (vtop (kpage) >> PTSHIFT < init_ram_pages);
  ASSERT (pg_ofs (kpage) == 0);

  bool ret = pagedir_set_page(pd, upage, kpage, !(el->flags & PAL_READONLY));
  ASSERT(ret);
  ASSERT(pagedir_get_page(pd, upage) != NULL);
  return ret;
}


void supp_pagedir_destroy(struct supp_pagedir *spd, uint32_t *pd){
  int i,j;
  for(i = 0; i < (1<<PDBITS); i++){
    struct supp_pagedir2 *spd2 = spd->entries[i];
    if(spd2 == NULL) continue;
    for(j = 0; j < (1<<PTBITS); j++){
      struct supp_pagedir_entry *e = spd2->entries[j];
      if(e == NULL) continue;
      supp_pagedir_destroy_page(spd, pd, e->upage);
    }
    free(spd2);
  }
  free(spd);
}

void supp_pagedir_destroy_page(struct supp_pagedir *spd, uint32_t *pd, void *upage){
  ASSERT(pd);ASSERT(spd);

  struct supp_pagedir_entry **elem = supp_pagedir_lookup(spd, upage, false);

  if(elem != NULL && *elem != NULL){
    struct supp_pagedir_entry *el = *elem;
    if(el->sector_t != BLOCK_SECTOR_T_ERROR)
      swap_read(el->sector_t, NULL);

    free(el);
    (*elem) = NULL;
  }else{
    NOT_REACHED();
  }

  void *kpage = pagedir_get_page(pd, upage);

  if(kpage)
    frame_free_page(kpage);

  pagedir_clear_page(pd, upage);
}

void supp_pagedir_set_prohibit(void *upage, bool prohibit){
  ASSERT(upage);
  struct supp_pagedir_entry *f = *supp_pagedir_lookup(thread_current()->supp_pagedir, upage, false);
  ASSERT(f);

  if(prohibit) f->flags |= PAL_PROHIBIT_CACHE;
  else if(!prohibit) f->flags &= ~PAL_PROHIBIT_CACHE;

  lock_acquire2(&f->lock);

  lock_acquire(frame_get_lock());

  void * kpage = pagedir_get_page(thread_current()->pagedir, pg_round_down(upage));
  if(kpage) frame_set_prohibit(kpage, prohibit);
  lock_release(frame_get_lock());

  if(kpage == NULL && prohibit){
    paging_activate_no_lock(*supp_pagedir_lookup(thread_current()->supp_pagedir, upage, false));
    kpage = pagedir_get_page(thread_current()->pagedir, pg_round_down(upage));
  }

  ASSERT(kpage);
  lock_release(&f->lock);
}
