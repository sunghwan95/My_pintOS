/* vm.c: Generic interface for virtual memory objects. */
//테스트주석
#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "include/userprog/process.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "vm/file.h"
#include <string.h>
uint64_t hash_func(const struct hash_elem *p_elem, void *aux UNUSED);
bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
void spt_dealloc(struct hash_elem *e, void *aux);
void remove_spt(struct hash_elem *elem, void *aux);
bool install_page(void *upage, void *kpage, bool writable);
static void vm_stack_growth(void *addr UNUSED);
void iter_munmap(struct supplemental_page_table *spt);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void){
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page){
	int ty = VM_TYPE(page->operations->type);
	switch (ty){
		case VM_UNINIT:
			return VM_TYPE(page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux){
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL){
		struct page *page = (struct page *)malloc(sizeof(struct page));
		if(page == NULL) return false;

		switch (VM_TYPE(type)){
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);
				break;
			default:
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
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED){
	/* TODO: Fill this function. */
	struct page *page = (struct page *)malloc(sizeof(struct page)); // 페이지 할당
	struct hash_elem *elem;
	page->va = pg_round_down(va);

	elem = hash_find(&spt->spt_hash, &page->hash_elem);
	free(page);

	if (elem == NULL){
		return NULL;
	}else{
		return hash_entry(elem, struct page, hash_elem);
	}
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED){
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL)
		return true;
	else
		return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page){
	if (!hash_delete(&spt->spt_hash, &page->hash_elem))
		return;
	vm_dealloc_page(page);
	return true;
}

// /* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void){
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	struct thread *curr = thread_current();
	struct list_elem* elem;
	for (elem = list_begin(&frame_table); elem != list_end(&frame_table); elem = list_next(elem)) {		
    	victim = list_entry(elem, struct frame, frame_elem);
        // 현재 frame의 PTE가 최근에 접근되었는지 확인
    	if (!pml4_is_accessed(curr->pml4, victim->page->va)) {
        	break; 
    	}
		pml4_set_accessed(curr->pml4, victim->page->va, 0);
		list_push_back(&frame_table, elem);
    }
	return victim;
}
/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/

static struct frame *
vm_evict_frame(void){
	struct frame *victim = vm_get_victim(); // 쳐낼 frame 페이지 찾기
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void){
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	// 반환받은 주소를 frame 구조체에 할당
	frame->kva = palloc_get_page(PAL_USER);
	if (frame->kva == NULL){
		frame = vm_evict_frame();
	}
	/* TODO: Fill this function. */
	frame->page = NULL;
	list_push_back(&frame_table, &frame->frame_elem);

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
/*
pg_round_down은 인자로 전달된 가상 주소를 페이지의 시작주소로 내림차순으로 반올림해서 반환(=새로운 페이지의 시작주소로 삼는다는 뜻)하는 함수임.
(스택은 아래로 자라기 때문에 주소를 내려줘야함.)
즉, 주어진 가상 주소를 페이지의 크기로 정렬하여 해당 페이지의 시작 주소를 반환한다.
pg_round_down 함수를 쓰면 페이지 단위로 가상 주소를 정렬할 수 있기 때문에 가상 주소를 페이지 크기로 분할할 수 있으며 페이지 테이블을 통해 데이터를 관리하는데 용이하다.
*/
static void
vm_stack_growth(void *addr UNUSED){
	struct thread *curr = thread_current();
	if(vm_alloc_page_with_initializer(VM_ANON, pg_round_down(addr), true, NULL, NULL)){
		curr->stack_bottom -= PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED){
}

bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(is_kernel_vaddr(addr) || !addr) return false;

	//void* rsp_stack = is_kernel_vaddr(f->rsp)? thread_current()->rsp_stack : f->rsp;

	if(not_present){
		if (USER_STACK - (1 << 20) <= addr && addr <= USER_STACK && f->rsp - 8 <= addr && thread_current()->stack_bottom >= addr){
			addr = thread_current()->stack_bottom - PGSIZE;
			vm_stack_growth(addr);
		}
	}
	return vm_claim_page(addr);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page){
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED){
	struct page *page = NULL;
	struct supplemental_page_table *spt = &thread_current()->spt;
	page = spt_find_page(spt, va); // spt에서 해당 va를 가진 페이지 찾기

	if (page == NULL)
		return false;

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page){
	struct frame *frame = vm_get_frame();
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 성공적으로 page가 매핑됐을 경우, 해당 page와 물리메모리 연결.
	// install_page함수 -> 가상메모리와 물리메모리를 매핑하는 함수.
	// (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable))
	if (install_page(page->va, frame->kva, page->writable)){
		return swap_in(page, frame->kva); // 매핑 성공시 swap-in
	}
	return false;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED){
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

// 페이지에대한 hash value 리턴
uint64_t hash_func(const struct hash_elem *p_elem, void *aux UNUSED){
	struct page *p = hash_entry(p_elem, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux){
	const struct page *p_a = hash_entry(a, struct page, hash_elem);
	const struct page *p_b = hash_entry(b, struct page, hash_elem);
	return p_a->va < p_b->va;
}

bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED){
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash); // i 초기화

	while (hash_next(&i)){ // next가 있는동안, vm_page_with_initiater 인자참고
		struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);

		enum vm_type type = page_get_type(parent_page);
		void *upage = parent_page->va;
		bool writable = parent_page->writable;
		vm_initializer *init = parent_page->uninit.init;
		void *aux = parent_page->uninit.aux;

		// 부모 페이지들의 정보를 저장한 뒤,자식이 가질 새로운 페이지를 생성해야합니다.생성을 위해 부모 페이지의 타입을 먼저 검사합니다.즉,부모 페이지가UNINIT 페이지인 경우와 그렇지 않은 경우를 나누어 페이지를 생성해줘야합니다.
		if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
			return false;

		struct page *child_page = spt_find_page(dst, upage); // 부모 주소 찾아서 복사.

		if (parent_page->frame != NULL){
			if (!vm_do_claim_page(child_page))
				return false;
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}
	return true;
}

/* 추가 페이지 테이블에서 리소스 보류 해제 */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED){
	hash_destroy(&spt->spt_hash, remove_spt);
}

void remove_spt(struct hash_elem *elem, void *aux){
	struct page *page = hash_entry(elem, struct page, hash_elem);
	vm_dealloc_page(page);
}

void iter_munmap(struct supplemental_page_table *spt){
	struct hash_iterator i;
	struct page* target;
	hash_first(&i, &spt->spt_hash);
	while (hash_next(&i)){
		target = hash_entry(hash_cur(&i), struct page, hash_elem);
		if (target->operations->type == VM_FILE){
			do_munmap(target->va);
		}
	}
}
