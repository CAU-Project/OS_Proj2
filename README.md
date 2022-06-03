# OS_Proj2
[운영체제] [Proj2] Page Allocation and Task Scheduling (Pintos)

## 프로젝트 개요
Project #2의 목적은 Pintos를 이용하여 페이지 할당과 스케줄링 알고리즘을 구현하는데 있다. Pintos는 기본 페이지 할당 알고리즘으로 First Fit을 사용하는데, 관련 코드의 수정 및 구현을 통해 Buddy System으로 페이지 할당 알고리즘을 교체하여야 한다. 또한 Pintos는 기본 스케줄러로 Round Robin(RR) 스케줄러를 사용하는데, 이를 관련 코드의 수정 및 구현을 통해 RR 스케줄러를 Multi-level Feedback Queue(MFQ) 스케줄러로 교체하여야 한다. 

- [2022OSProj2_vfinal.docx 참고]

# 1. Page Allocation

연관 함수들

**palloc.h**

```c
/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero page contents. */
    PAL_USER = 004              /* User page. */
  };

void palloc_init (size_t user_page_limit); //페이지 할당자를 초기화. 커널 페이지와 사용자 페이지로 나눠서 각자 할당
void *palloc_get_page (enum palloc_flags); // 단일 페이지 할당 
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt); // page_cnt 인자로 주어진 수 만큼 페이지 할당. 할당은 연속된 메모리 공간을 스캔한 뒤에 연속된 페이지를 할당 해 준다.
void palloc_free_page (void *); // 단일 페이지 free
void palloc_free_multiple (void *, size_t page_cnt); // 여러 페이지 free
void palloc_get_status (enum palloc_flags flags); // 할당된 페이지들의 상태를 출력할 함수. 미구현 상태
```


## 1.1 palloc_get_multiple

pintos의 기본 페이지 할당 함수인 palloc_get_multiple()함수를 분석해 본다.

```c
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
    //PAL_USER 플래그가 설정되어 있으면 user_pool로, 아니면 kernel_pool로
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;
    
  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  // 이 부분이 first fit 방식
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
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

  return pages;
}
```
### 1.1.1 struct pool

**pool** 구조체. 상호배제를 위한 lock, 풀 공간의 사용 여부 표시를 위한 bitmap, 시작 주소를 가지고 있는 base 가 있다.
```c
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct bitmap *used_map;            /* Bitmap of free pages. */
    uint8_t *base;                      /* Base of pool. */
  };


```

### 1.1.2 bitmap_scan()
페이지 할당을 위해서 pool의 비트맵을 스캔하는 함수이다.

```c
// bitmap_scan함수를 이용했기 때문에 pintos에서 페이지 할당은 first fit 정책인 것이다. 내용을 살펴보면, bitmap_scan은 start 부터 인덱스를 증가시키면서 사용가능한 bitmap을 찾고 있다. 즉 사용가능한 메모리를 처음부터 스캔해서 최초(first) 사용 가능한 메모리 공간을 할당한다. 
size_t bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value) 
{
  ASSERT (b != NULL);
  ASSERT (start <= b->bit_cnt);

  if (cnt <= b->bit_cnt) 
    {
      size_t last = b->bit_cnt - cnt;
      size_t i;
      for (i = start; i <= last; i++)
        if (!bitmap_contains (b, i, cnt, !value))
          return i; 
    }
  return BITMAP_ERROR;
}

// bitmap_scan 함수를 통해서 연속된 false(value)를 찾으면 해당 부분을 전부 반전(flip)하고 시작 idx를 반환한다.
size_t bitmap_scan_and_flip (struct bitmap *b, size_t start, size_t cnt, bool value)
{
  size_t idx = bitmap_scan (b, start, cnt, value);
  if (idx != BITMAP_ERROR) 
    bitmap_set_multiple (b, idx, cnt, !value);
  return idx;
}
```

## 1.2 buddy system

분석결과 bitmap_scan 함수를 사용하지 않고 다른 함수를 만들어서 bitmap_scan함수를 대체해야 한다. 해당 함수를 bitmap_scan_and_flip_buddy()로 만든다.

```c
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
    // .. 생략
  lock_acquire (&pool->lock);
  // 이 부분이 first fit 방식
  /*page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);*/

  page_idx = bitmap_scan_and_flip_buddy (pool->used_map, 0, page_cnt, false);
  
  lock_release (&pool->lock);

    // .. 생략


  return pages;
}

```


# 2. Mutilevel Feedback Queue
Pintos에 기본적으로 구현되어 있는 스케줄링 정책은 Round Robin 방식이다. 이를 MFQ방식으로 바꿔야 한다.

먼저 쓰레드의 생성부터 어떻게 정책이 적용되고 있는지 분석한다.

## 2.1 Thead_init()

init.c에서 가장 먼저 호출하는 함수이다. Thread 관련 설정을 위한 초기 설정 함수로써 init_thread로 main 쓰레드를 초기화 하는 것을 볼 수 있다. 모든 쓰레드는 thread_create() -> init_thread()를 통해서 생성 및 초기화가 진행 된다.

```c
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}
```



## 2.2 init_thread()
이전에 설명한 init_thread()함수이다. 눈에 띄는 점은 마지막에 list_push_back(&all_list,&t->allelem); 함수이다. 음 , 이 함수는 건드릴 필요 없이 그대로 사용 하면 될 것 같다.

```c
/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}


```

## 2.3 thread_create()
thread를 만드는 함수이다. 초기 생성직후 어떠한 작업을 하는지 보자.

변수들은 건드릴 게 없을 거 같고, 
1. 페이지 할당(커널 영역)
2. init_thread()로 이름, 우선순위 설정 해주고(변경 필요 x)
3. 스택 프레임 설정(변경x)
4. thread_unblock(t) -> 분석 필요

마지막에 thread_unblock(t) 를 호출하는데 이게 무슨 함수인지 보자.

```c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}

```


## 2.4 thread_unblock(t)
주석 부분을 읽어보면 쓰레드가 blocked 상태인 경우에 이를 Ready 상태로 바꿔준다. 이부분은 바꿔야 겠다. 기존에는 하나의 큐인 ready_list에서 모든 스케줄링을 진행했지만, 이제는 MFQ를 사용하기 때문에 시작하면 할당된 priority에 따라서 큐를 결정해야 한다.

또한 고려사항은 해당 함수가 시작할때만 호출되는 함수인지 확인해야 한다.
참조 되는 부분들을 보니 thread_create()와 thread_wakeup()이다.

두 부분에서 다르게 작동해야 할까? 
No. Tread가 block되거나 ready 상태에서 큐를 이동할 때, priority를 바꿔주자. 그러면 block -> ready로 갈때 priority만 보고 큐를 선택해 주면 되니까.

- 문제 요구사항에서 block될 때, 바로 상위 우선순위 큐로 이동하라고 했으니까 block되는 코드에서 priority 1 뺴주자!

```c
/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem); // 이거 바꿔야함. 
  t->status = THREAD_READY;
  intr_set_level (old_level);
}
```

여기까지 정리해 보면

프로그램이 시작되어서 thread_create() -> init_thread() -> thread_unblock() 까지 수행돼서 priority에 맞도록 MFQ에 들어가게 된다.

이 이후에는 어떻게 스케줄링이 진행되는지 더 찾아보자.

