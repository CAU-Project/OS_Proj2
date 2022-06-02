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