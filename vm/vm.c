/* vm.c: Generic interface for virtual memory objects. */
#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "include/userprog/process.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "vm/file.h"
#include <string.h>
static unsigned hash_func(const struct hash_elem *p_elem, void *aux UNUSED);
static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
void remove_spt(struct hash_elem *elem, void *aux);
bool install_page(void *upage, void *kpage, bool writable);
static void vm_stack_growth(void *addr UNUSED);
void destroy_frame_table(void);

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
	lock_init(&frame_lock);
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
	struct page page;
	struct hash_elem *elem;
	page.va = pg_round_down(va);

	elem = hash_find(&spt->spt_hash, &page.hash_elem);

	if (elem == NULL){
		return NULL;
	}
	return hash_entry(elem, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED){
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL)
		return true;
	else
		return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page){
	vm_dealloc_page(page);
}

// /* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void){
	/* TODO: The policy for eviction is up to you. */
	// struct frame *victim = NULL;
	// struct list_elem* elem;

	// lock_acquire(&frame_lock);
	// // frame_table을 순회하면서 가장 액세스가 오래된 페이지를 찾음.
	// for (elem = list_begin(&frame_table); elem != list_end(&frame_table); elem = list_next(elem)) {		
    // 	struct frame *curr_frame = list_entry(elem, struct frame, frame_elem);

    // 	if (pml4_is_accessed(thread_current()->pml4, victim->page->va)) {//현재 frame의 pte가 최근에 접근되었다면
	// 		pml4_set_accessed(thread_current()->pml4, victim->page->va, 0);
	// 		///list_push_back(&frame_table, elem);
    // 	}else{
	// 		victim = curr_frame;
	// 		list_remove(elem);
	// 		break;
	// 	}
    // }

	// if(victim == NULL){// 모든 페이지가 최근에 접근된 상태라면
	// 	elem = list_pop_front(&frame_table);
	// 	victim = list_entry(elem, struct frame, frame_elem);
	// }	
	// lock_release(&frame_lock);
	// return victim;

	struct frame *victim = NULL;
	struct list_elem* elem;
	lock_acquire(&frame_lock);
	for (elem = list_begin(&frame_table); elem != list_end(&frame_table); elem = list_next(elem)) {		
    	struct frame *curr_frame = list_entry(elem, struct frame, frame_elem);

    	if (!pml4_is_accessed(thread_current()->pml4, curr_frame->page->va)) {
			victim = curr_frame;
			list_remove(elem);
        	break;
    	}
		pml4_set_accessed(thread_current()->pml4, curr_frame->page->va, 0);
    }

	if(victim == NULL){
		elem = list_pop_front(&frame_table);
		victim = list_entry(elem, struct frame, frame_elem);
	}
	lock_release(&frame_lock);
	return victim;
}
/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/

static struct frame *
vm_evict_frame(void){
	struct frame *victim = vm_get_victim(); // 쳐낼 frame 페이지 찾기
	if(victim == NULL)
		return NULL;
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	victim->page = NULL;
	memset(victim->kva, 0, PGSIZE);
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
	addr = pg_round_down(addr);
	while(addr < curr->stack_bottom){
		vm_alloc_page_with_initializer(VM_ANON, addr, true, NULL, NULL);
		vm_claim_page(addr);
		addr += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED){
	void *parent_kva = page->frame->kva;
	page->frame->kva = palloc_get_page(PAL_USER);

	memcpy(page->frame->kva, parent_kva, PGSIZE);
	if (!pml4_set_page(thread_current()->pml4, page->va, page->frame->kva, page->copy_writable))
		palloc_free_page(page->frame->kva);
	return true;
}

bool vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED){
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;	
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(is_kernel_vaddr(addr) || !addr) 
		return false;

	page = spt_find_page(spt, addr);
	if(page == NULL){
		void *rsp = !user ? thread_current()->tf.rsp : f->rsp;
		if (rsp - (1 << 3) <= addr && addr <= thread_current()->stack_bottom){
			vm_stack_growth(addr);
			thread_current()->stack_bottom = pg_round_down(addr);
			return true;
		}		
	}else{
		if(write && !page->writable)
			return false;
		if(write && page->copy_writable)
			return vm_handle_wp(page);
		vm_do_claim_page(page);
		return true;
	}
	return false;
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
	if(frame == NULL)
		return false;
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
static unsigned hash_func(const struct hash_elem *p_elem, void *aux UNUSED){
	struct page *p = hash_entry(p_elem, struct page, hash_elem);
	return hash_int(p->va);
}

static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux){
	const struct page *p_a = hash_entry(a, struct page, hash_elem);
	const struct page *p_b = hash_entry(b, struct page, hash_elem);
	return p_a->va < p_b->va;
}

bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED){
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);

	while (hash_next(&i)){
		struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		struct page *copy_page = NULL;
		switch(VM_TYPE(parent_page->operations->type)){
			case VM_UNINIT:
				if(VM_TYPE(parent_page->uninit.type == VM_ANON)){
					struct segment *seg = (struct segment*)malloc(sizeof(struct segment));
					memcpy(seg, parent_page->uninit.aux, sizeof(struct segment));
					seg->file = file_duplicate(seg->file);
					if(!vm_alloc_page_with_initializer(parent_page->uninit.type, parent_page->va, parent_page->writable, parent_page->uninit.init, seg))
						free(seg);
				}
				break;
			case VM_ANON:
				vm_alloc_page(parent_page->operations->type, parent_page->va, parent_page->writable);
				copy_page = spt_find_page(dst, parent_page->va);
				if(!copy_page)
					return false;
				
				copy_page->copy_writable = parent_page->writable;
				struct frame *copy_frame = malloc(sizeof(struct frame));
				copy_page->frame = copy_frame;
				copy_frame->page = copy_page;
				copy_frame->kva = parent_page->frame->kva;
				
				struct thread *curr = thread_current();
				lock_acquire(&frame_lock);
				list_push_back(&frame_table, &copy_frame->frame_elem);
				lock_release(&frame_lock);

				if(!pml4_set_page(curr->pml4, copy_page->va, copy_frame->kva, 0)){
					free(copy_frame);
					return false;
				}
				swap_in(copy_page, copy_frame->kva);
				break;
			default:
				break;
		}
	}
	return true;
}

/* 추가 페이지 테이블에서 리소스 보류 해제 */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED){
	if(!hash_empty(&spt->spt_hash)){
		struct hash_iterator i;
		struct frame *frame;
		hash_first(&i, &spt->spt_hash);
		while(hash_next(&i)){
			struct page *target = hash_entry(hash_cur(&i), struct page, hash_elem);
			frame = target->frame;
			if(target->operations->type == VM_FILE)
				do_munmap(target->va);
		}
		hash_destroy(&spt->spt_hash, remove_spt);
		free(frame);
	}
}

void remove_spt(struct hash_elem *elem, void *aux){
	struct page *page = hash_entry(elem, struct page, hash_elem);
	ASSERT(is_user_vaddr(page->va));
	ASSERT(is_kernel_vaddr(page));
	free(page);
}

void destroy_frame_table(void){
	struct list_elem *elem;
	struct frame *frame;
	while(!list_empty(&frame_table)){
		elem = list_pop_front(&frame_table);
		frame = list_entry(elem, struct frame, frame_elem);
		free(frame);
	}
}
