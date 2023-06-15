/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */

}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}
/* project 3-----------------------------------------------------*/
// 페이지의 va를 해쉬화 하는 함수.
uint64_t hash_func(const struct hash_elem *p_elem, void* aux UNUSED){
	struct page *p = hash_entry (p_elem, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void* aux){
	const struct page* p_a = hash_entry(a, struct page, hash_elem);
	const struct page* p_b = hash_entry(b, struct page, hash_elem);
	return p_a->va < p_b->va;
}
/*---------------------------------------------------------------*/

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		struct page *page = (struct page*)malloc(sizeof(struct page));

		switch (VM_TYPE(type)){
			case VM_ANON:
				uninit_new(page, page->va, init, type, aux, anon_initializer(page, type, aux));
				break;
			case VM_FILE:
				uninit_new(page, page->va, init, type, aux, file_backed_initializer(page, type, aux));
				break;
		}
		page->writable = writable;
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
		/* TODO: Fill this function. */
	struct page *page = (struct page *)malloc(sizeof(struct page)); // 페이지 할당
	struct hash_elem *spt_elem;

	page->va = pg_round_down(va);						// 페이지 번호 얻기
	if (hash_find(&spt->spt_hash, &page->hash_elem) == NULL) // 존재안하면 NULL
	{
		free(page);
		return NULL;
	}
	else // 하면
	{
		spt_elem = hash_find(&spt->spt_hash, &page->hash_elem); // hash_entry 리턴
		free(page);
		return hash_entry(spt_elem, struct page, hash_elem);
	}
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {

	/* TODO: Fill this function. */
	if(hash_insert(spt, &page->hash_elem)) return true;
	else return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	if(hash_delete(spt, &page->hash_elem)){
		vm_dealloc_page (page);
		return true;
	}else{
		return false;
	}
}

/*
쳐낼 프레임 찾기
->여기서 페이지 교체 알고리즘을 적용해야하는데 뭘 적용해야할까 하다가
  linked_list의 LRU기법을 선택하기로함.
  ->왜 why? frame을 struct list 형태로 선언했기 때문에.(struct list 는 linked-list 형태를 가짐.)
*/
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct thread* curr = thread_current();
	struct list_elem *elem;

    // frame_list를 순회하면서 가장 최근에 사용되지 않은 frame을 찾음
    for (elem = list_begin(&frame_list); elem != list_end(&frame_list); elem = list_next(elem)) {

        victim = list_entry(elem, struct frame, frame_elem);
        // 현재 frame의 PTE가 최근에 접근되었는지 확인
        if (pml4_is_accessed(curr->pml4, victim->page->va)) { // 해당 frame의 access-bit가 1이라면
            continue; 
        } else {
            return victim;
        }
    }

	return victim; // 맞게 구현했는지 모르겠음... 확인 좀 부탁해용 ㅠ0ㅠ
}

/* 
쳐낼 frame을 선택하고 swap-out을 진행해서 frame 반환
실패시 NULL 반환
*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim (); // 쳐낼 frame 찾기
	/* TODO: swap out the victim and return the evicted frame. */
	if(swap_out(victim->page)) return victim;
	else return NULL;
}

/* 
palloc_get_page 함수를 호출함으로써 당신의 메모리 풀에서 새로운 물리메모리 페이지를 가져옵니다.
유저 메모리 풀에서 페이지를 성공적으로 가져오면, 프레임을 할당하고 프레임 구조체의 멤버들을 초기화한 후 해당 프레임을 반환합니다. 
당신이 frame *vm_get_frame  함수를 구현한 후에는 모든 유저 공간 페이지들을 이 함수를 통해 할당해야 합니다.
지금으로서는 페이지 할당이 실패했을 경우의 swap out을 할 필요가 없습니다. 
*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = palloc_get_page(PAL_ZERO | PAL_USER);
	/* TODO: Fill this function. */
	/*
	!!!!중요!!!! -----> 이걸 알아야 코딩 가능.
	pintos에서 kva는 물리메모리 주소라고 생각하면 편함.
	우선 frame 구조체 안에 kva가 선언이 되있고 pintos에서는 실제 dram이 장착되어서 구동되는게 아니기 때문에.
	*/
	if(frame->kva == NULL){ // 쓸 수 있는 물리메모리 공간이 없다면
		frame = vm_evict_frame();
		frame->page = NULL; // frame 비워줌(초기화).
		return frame;
	}

	// frame 비워주고 frame_list에 넣어줌.
	frame->page = NULL; 
	list_push_back(&frame_list, &frame->frame_elem);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* 
인자로 주어진 va에 page를 할당하고 그 page에 frame을 할당할 수 있도록 return하는 함수.
*/
bool
vm_claim_page (void *va UNUSED) {
	/* TODO: Fill this function */
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = spt_find_page(spt, va); // spt에서 해당 va를 가진 페이지 찾기

	if(page ==NULL) return false;

	return vm_do_claim_page (page); 
}

/* 
인자로 주어진 page에 물리 메모리 프레임을 할당
 */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame (); // 사용가능한 frame 호출

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 성공적으로 page가 매핑됐을 경우, 해당 page와 물리메모리 연결.
	// install_page함수 -> 가상메모리와 물리메모리를 매핑하는 함수.
	if(install_page(page->va, frame->kva, page->writable)) return swap_in (page, frame->kva); // 매핑 성공시 swap-in
	return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
