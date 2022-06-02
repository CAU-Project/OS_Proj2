#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct bitmap *used_map;            /* Bitmap of free pages. */
    uint8_t *base;                      /* Base of pool. */
  };

/* buddy system. */
struct buddy {
    size_t size;
    size_t longest[1024];
};

size_t bitmap_scan_and_flip_buddy (struct buddy* buddy,struct bitmap *b, size_t start, size_t cnt, bool value);

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;
static struct buddy b1, b2;
static struct buddy *kernel_buddy, *user_buddy;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);


/* Proj2: Implemenation Buddy System */
/*
 * Description:
 *   Implement buddy system for memory management
 *
 * Idea & part of the code are from https://github.com/lotabout/buddy-system
 */

#define max(a, b) (((a)>(b))?(a):(b))
#define min(a, b) (((a)<(b))?(a):(b))


static inline int left_child(int index)
{
    /* index * 2 + 1 */
    return ((index << 1) + 1); 
}

static inline int right_child(int index)
{
    /* index * 2 + 2 */
    return ((index << 1) + 2);
}

static inline int parent(int index)
{
    /* (index+1)/2 - 1 */
    return (((index+1)>>1) - 1);
}

static inline bool is_power_of_2(int index)
{
    return !(index & (index - 1));
}

static inline size_t next_power_of_2(size_t size)
{
    /* depend on the fact that size < 2^32 */
    size -= 1;
    size |= (size >> 1);
    size |= (size >> 2);
    size |= (size >> 4);
    size |= (size >> 8);
    size |= (size >> 16);
    return size + 1;
}

/** allocate a new buddy structure 
 * @param page_cnt number of fragments of the memory to be managed 
 * @return pointer to the allocated buddy structure */
struct buddy *buddy_new(size_t page_cnt, char* name)
{
//    printf("[buddy_new] page_cnt : %d, name : %s\n",page_cnt,name);
    struct buddy *self = NULL;
    size_t node_size;

    int i;

    // set buddy size to next power of 2
    page_cnt = next_power_of_2(page_cnt);
//    printf("[buddy_new] next_power of 2 page_cnt : %d\n",page_cnt);

    /* alloacte an array to represent a complete binary tree */
    if(!strcmp(name,"kernel pool")){
      self = &b1;
    }else{
      self = &b2;
    }
    self->size = page_cnt;
    node_size = page_cnt * 2;
    
    /* initialize *longest* array for buddy structure */
    int iter_end = page_cnt * 2 - 1;
    for (i = 0; i < iter_end; i++) {
        if (is_power_of_2(i+1)) {
            node_size >>= 1;
        }
        self->longest[i] = node_size;
    }
//    printf("[buddy_new] self->size : %d\n",self->size);
    return self;
}

/* choose the child with smaller longest value which is still larger
 * than *size* */
size_t choose_better_child(struct buddy *self, size_t index, size_t size)
{
    struct compound {
        size_t size;
        size_t index;
    } children[2];
    children[0].index = left_child(index);
    children[0].size = self->longest[children[0].index];
    children[1].index = right_child(index);
    children[1].size = self->longest[children[1].index];

    int min_idx = (children[0].size <= children[1].size) ? 0: 1;

    if (size > children[min_idx].size) {
        min_idx = 1 - min_idx;
    }
    
    return children[min_idx].index;
}

/** allocate *size* from a buddy system *self* 
 * @return the offset from the beginning of memory to be managed */
int buddy_alloc(struct buddy *self, size_t size)
{
//    printf("[buddy_alloc] self->size : %d, size : %d\n",self->size,size);

    if (self == NULL || self->size < size) {
        return -1;
    }
    size = next_power_of_2(size);

    size_t index = 0;
    if (self->longest[index] < size) {
        return -1;
    }

    /* search recursively for the child */
    size_t node_size = 0;
    for (node_size = self->size; node_size != size; node_size >>= 1) {
        /* choose the child with smaller longest value which is still larger
         * than *size* */
        /* TODO */
        index = choose_better_child(self, index, size);
    }

    /* update the *longest* value back */
    self->longest[index] = 0;
    int offset = (index + 1)*node_size - self->size;

    while (index) {
        index = parent(index);
        self->longest[index] = 
            max(self->longest[left_child(index)],
                self->longest[right_child(index)]);
    }
//    printf("[buddy alloc] retrun offset = %d\n",offset);
    return offset;
}

void buddy_free(struct buddy *self, int offset)
{
    if (self == NULL || offset < 0 || offset > self->size) {
        return;
    }

    size_t node_size;
    size_t index;

    /* get the corresponding index from offset */
    node_size = 1;
    index = offset + self->size - 1;

    for (; self->longest[index] != 0; index = parent(index)) {
        node_size <<= 1;    /* node_size *= 2; */

        if (index == 0) {
            break;
        }
    }

    self->longest[index] = node_size;

    while (index) {
        index = parent(index);
        node_size <<= 1;

        size_t left_longest = self->longest[left_child(index)];
        size_t right_longest = self->longest[right_child(index)];

        if (left_longest + right_longest == node_size) {
            self->longest[index] = node_size;
        } else {
            self->longest[index] = max(left_longest, right_longest);
        }
    }
}



size_t bitmap_scan_and_flip_buddy (struct buddy* buddy,struct bitmap *b, size_t start, size_t cnt, bool value)
{
  // get index from buddy
  size_t idx = buddy_alloc(buddy,cnt);

  // count must be power of 2.
  cnt = next_power_of_2(cnt);

  // flip bitmap [idx:idx+cnt]
  if (idx != BITMAP_ERROR) 
    bitmap_set_multiple (b, idx, cnt, !value);

//  printf("[bitmap_flip_buddy] : idx = %d, cnt = %d\n",idx,cnt);
  return idx;
}


/* Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
  /* Free memory starts at 1 MB and runs to the end of RAM. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
//  printf("[palloc_init] init_ram_pages : %d\n",init_ram_pages);
//  printf("[palloc_init] free_start value : %p\n",free_start);
//  printf("[palloc_init] free_end value : %p\n",free_end);

  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;


  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");

  
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
//  printf("[palloc_get_multiple] page_cnt : %zu, flags : %d\n",page_cnt,flags);
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  struct buddy *buddy = flags & PAL_USER ?user_buddy : kernel_buddy;

//  printf("[palloc_get_multiple] buddy->size : %d\n",buddy->size);
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
//  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  page_idx = bitmap_scan_and_flip_buddy(buddy,pool->used_map,0,page_cnt,false);
  lock_release (&pool->lock);


  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }
//  printf("[palloc_get_multiple] palloc at memory : %p\n",pages);
  printf("\033[31m[palloc] page is allocated in idx: %d, page_cnt : %d\n\033[0m",page_idx,page_cnt);
  return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
//  printf("[palloc_free_multiple] palloc_free at memory : %p\n",pages);
  struct pool *pool;
  struct buddy *buddy;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages)){
    pool = &kernel_pool;
    buddy = kernel_buddy;
  }
  else if (page_from_pool (&user_pool, pages)){
    pool = &user_pool;
    buddy = user_buddy;
  }
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  /*buddy system*/
  page_cnt = next_power_of_2(page_cnt);
  buddy_free(buddy,page_idx);

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);

//  printf("\033[32m[pfree] deallocate page in idx: %d, page_cnt : %d\n\033[0m",page_idx,page_cnt);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;

//  printf ("[init_pool] %zu pages available in %s.\n", page_cnt, name);
//  printf ("[init_pool] %zu bitmap_size in %s.\n", bm_pages, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE;

  /* Initialize buddy system. */
  if(!strcmp(name,"kernel pool")){
//    printf("[init_pool] kernel buddy_new call, page_cnt : %d\n",page_cnt);
    kernel_buddy = buddy_new(page_cnt,name);
//    printf("[init_pool] kernel buddy -> size : %d\n",kernel_buddy->size);
  }
  if(!strcmp(name,"user pool")){
//    printf("[init_pool] user buddy_new call, page_cnt : %d\n",page_cnt);
    user_buddy = buddy_new(page_cnt,name);
//    printf("[init_pool] user_buddy -> size : %d\n",user_buddy->size);
  }


}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);

  return page_no >= start_page && page_no < end_page;
}

/* Obtains a status of the page pool */
void
palloc_get_status (enum palloc_flags flags)
{
  //IMPLEMENT THIS
  //PAGE STATUS 0 if FREE, 1 if USED
  //32 PAGE STATUS PER LINE

  struct bitmap *kernel_bitmap = kernel_pool.used_map;
  struct bitmap *user_bitmap = user_pool.used_map;

  size_t kernel_bitmap_size = bitmap_size(kernel_bitmap);
  size_t user_bitmap_size = bitmap_size(user_bitmap);
  
  bool is_user = flags & PAL_USER ? true : false;

  if(!is_user){
    // print kernel pool's page
    printf("\033[33m======================= palloc_get_status:Kernel =======================\n\033[0m");
    printf("Kernel area page count : %d\n",kernel_bitmap_size);
    printf("%8d%16d%16d%16d%16d\n",0,8,16,24,32);
    unsigned int i;
    for(i=0; i< kernel_bitmap_size / 32 ;i++){
        printf("[%3d]  ",32*i);
        for(int j=0; j<32; j++){
          printf("%zu ",bitmap_test(kernel_bitmap,32*i+j));
        }
      printf("\n");
    }
    printf("[%3d]  ",32*i);
    for(unsigned int j=0; j <kernel_bitmap_size % 32;j++){
      printf("%zu ",bitmap_test(kernel_bitmap,32*i+j));
    }
    printf("\n");
  }else{
    // print user pool's page
    printf("\033[34m======================= palloc_get_status:user =======================\n\033[0m");
    printf("User area page count : %d\n",user_bitmap_size);
    printf("%8d%16d%16d%16d%16d\n",0,8,16,24,32);
    unsigned int i;
    for(i=0; i< user_bitmap_size / 32 ;i++){
        printf("[%3d]  ",32*i);
        for(unsigned int j=0; j<32;j++){
          printf("%zu ",bitmap_test(user_bitmap,32*i+j));
        }
      printf("\n");
    }
    printf("[%3d]  ",32*i);
    for(unsigned int j=0; j<user_bitmap_size % 32; j++){
        printf("%zu ",bitmap_test(user_bitmap,32*i+j));
    }
    
    printf("\n");
    }
}
