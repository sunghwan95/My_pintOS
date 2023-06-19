/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "include/userprog/process.h"
#include "threads/mmu.h"
#include <string.h>
uint64_t hash_func(const struct hash_elem *p_elem, void *aux UNUSED);
bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
void spt_dealloc(struct hash_elem *e, void *aux);
void remove_spt(struct hash_elem *elem, void *aux);
struct list frame_table;
struct lock frame_lock; // lock_aquire(), realese용
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
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

/* 이니셜라이저를 사용하여 보류 중인 페이지 개체를 만듭니다. 생성하려는 경우
 * 페이지, 직접 생성하지 말고 이 기능을 통해 생성하거나
 * "vm_alloc_page". */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		struct page *page = (struct page *)malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
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
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	/* TODO: Fill this function. */
	struct page *page = (struct page *)malloc(sizeof(struct page)); // 페이지 할당
	struct hash_elem *elem;
	page->va = pg_round_down(va);

	elem = hash_find(&spt->spt_hash, &page->hash_elem);
	free(page);

	if (elem == NULL)
	{
		return NULL;
	}
	else
	{
		return hash_entry(elem, struct page, hash_elem);
	}
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL)
		return true;
	else
		return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

// /* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}
// Sangju
// static struct frame *
// vm_get_victim(void) // 희생할 한개 프레임 선택함수 , FIFO 사용
// {
// 	struct frame *victim = list_pop_front(&frame_table);
// 	/* TODO: The policy for eviction is up to you. */
// 	return victim;
// }

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
// Sangju
static struct frame *
vm_evict_frame(void)
{
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
vm_get_frame(void)
{
	// Sangju
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	// Sangju
	// 반환받은 주소를 frame 구조체에 할당
	frame->kva = palloc_get_page(PAL_USER);
	if (frame->kva == NULL)
	{
		PANIC("todo");
	}
	/* TODO: Fill this function. */
	/*
	!!!!중요!!!! -----> 이걸 알아야 코딩 가능.
	pintos에서 kva는 물리메모리 주소라고 생각하면 편함.
	우선 frame 구조체 안에 kva가 선언이 되있고 pintos에서는 실제 dram이 장착되어서 구동되는게 아니기 때문에.
	*/
	// if (frame->kva == NULL)
	// { // 쓸 수 있는 물리메모리 공간이 없다면
	// 	frame = vm_evict_frame();
	// 	frame->page = NULL; // frame 비워줌(초기화).
	// 	return frame;
	// }

	// frame 비워주고 frame_table에 넣어줌.
	// lock_acquire(&frame_lock);
	// list_push_back(&frame_table, &frame->frame_elem);
	// lock_release(&frame_lock);
	frame->page = NULL;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}
// v2
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	bool success = false;
	if (is_kernel_vaddr(addr))
		return false;
	page = spt_find_page(spt, addr);
	if (page == NULL)
		return false;
	else
	{
		success = vm_do_claim_page(page);
		return success;
	}

	return success;
}
/* Return true on success */
// v1
// bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
// 						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
// {
// 	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
// 	struct page *page = NULL;
// 	/* TODO: Validate the fault */
// 	/* TODO: Your code goes here */
// 	if (is_kernel_vaddr(addr))
// 		return false;

// 	void *rsp_stack;
// 	if (is_kernel_vaddr(f->rsp))
// 	{
// 		rsp_stack = thread_current()->rsp_stack;
// 	}
// 	else
// 	{
// 		rsp_stack = f->rsp;
// 	}

// 	if (not_present)
// 	{
// 		if (!vm_claim_page(addr))
// 		{
// 			if (rsp_stack - 8 <= addr && addr <= USER_STACK)
// 			{
// 				vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
// 				return true;
// 			}
// 			return false;
// 		}
// 		else
// 		{
// 			return true;
// 		}
// 	}
// 	return false;
// 	// return vm_do_claim_page (page);
// }

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	struct supplemental_page_table *spt = &thread_current()->spt;
	page = spt_find_page(spt, va); // spt에서 해당 va를 가진 페이지 찾기

	if (page == NULL)
		return false;

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	struct thread *t = thread_current();
	if (frame == NULL)
	{
		return false;
	}
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 성공적으로 page가 매핑됐을 경우, 해당 page와 물리메모리 연결.
	// install_page함수 -> 가상메모리와 물리메모리를 매핑하는 함수.
	// (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable))
	if ((pml4_get_page(t->pml4, page->va) == NULL && pml4_set_page(t->pml4, page->va, frame->kva, page->writable)))
	{
		return swap_in(page, frame->kva); // 매핑 성공시 swap-in
	}
	// if (install_page(page->va, frame->kva, page->writable))

	return false;
}

// bool install_page(void *upage, void *kpage, bool writable)
// {
//    struct thread *t = thread_current();

//    /* Verify that there's not already a page at that virtual
//     * address, then map our page there. */
//    return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
// }

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

// Sangju hash_init용
// 페이지에대한 hash value 리턴
uint64_t hash_func(const struct hash_elem *p_elem, void *aux UNUSED)
{
	struct page *p = hash_entry(p_elem, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}
// Sangju hash_init용
bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	const struct page *p_a = hash_entry(a, struct page, hash_elem);
	const struct page *p_b = hash_entry(b, struct page, hash_elem);
	return p_a->va < p_b->va;
}
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator hash_iter;
	struct page *parent_page;
	bool success = false;
	hash_first(&hash_iter, &src->spt_hash);
	while (hash_next(&hash_iter))
	{
		parent_page = hash_entry(hash_cur(&hash_iter), struct page, hash_elem);

		success = vm_alloc_page_with_initializer(parent_page->uninit.type,
												 parent_page->va,
												 parent_page->writable,
												 parent_page->uninit.init,
												 parent_page->uninit.aux);
		if (!success)
			return false;
		struct page *child_page = spt_find_page(dst, parent_page->va);
		if (parent_page->frame)
		{
			success = vm_do_claim_page(child_page);
			if (!success)
			{
				return false;
			}
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}
	return success;
}
// v2
// bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
// 								  struct supplemental_page_table *src UNUSED)
// {
// 	struct hash_iterator i;
// 	hash_first(&i, &src->spt_hash); // i 초기화

// 	while (hash_next(&i))
// 	{ // next가 있는동안, vm_page_with_initiater 인자참고
// 		struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);

// 		enum vm_type type = page_get_type(parent_page);
// 		void *upage = parent_page->va;
// 		bool writable = parent_page->writable;
// 		vm_initializer *init = parent_page->uninit.init;
// 		void *aux = parent_page->uninit.aux;

// 		// vm_alloc_page_with_initializer(type, upage, writable, init, aux);
// 		// 부모 페이지들의 정보를 저장한 뒤,자식이 가질 새로운 페이지를 생성해야합니다.생성을 위해 부모 페이지의 타입을 먼저 검사합니다.즉,부모 페이지가UNINIT 페이지인 경우와 그렇지 않은 경우를 나누어 페이지를 생성해줘야합니다.
// 		if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
// 			return false;

// 		struct page *child_page = spt_find_page(dst, upage); // 부모 주소 찾아서 복사.

// 		if (child_page->frame != NULL)
// 		{
// 			if (!vm_do_claim_page(child_page))
// 				return false;
// 			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
// 		}
// 	}
// 	return true;
// }
/* 보조 페이지 표를 src에서 dst 로 복사합니다*/
// bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
// 								  struct supplemental_page_table *src UNUSED)
// {

// 	struct hash_iterator i;
// 	hash_first(&i, &src->spt_hash); // i 초기화

// 	struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);
// 	bool success = false;

// 	enum vm_type type = page_get_type(parent_page);
// 	void *upage = parent_page->va;
// 	bool writable = parent_page->writable;
// 	vm_initializer *init = parent_page->uninit.init;
// 	void *aux = parent_page->uninit.aux;
// 	while (hash_next(&i))
// 	{ // next가 있는동안, vm_page_with_initiater 인자참고

// 		success = vm_alloc_page_with_initializer(type, upage, writable, init, aux);
// 		// vm_alloc_page_with_initializer(type, upage, writable, init, aux);
// 		// 부모 페이지들의 정보를 저장한 뒤,자식이 가질 새로운 페이지를 생성해야합니다.생성을 위해 부모 페이지의 타입을 먼저 검사합니다.즉,부모 페이지가UNINIT 페이지인 경우와 그렇지 않은 경우를 나누어 페이지를 생성해줘야합니다.
// 		if (!success)
// 		{
// 			return false;
// 		}
// 		struct page *child_page = spt_find_page(dst, upage);
// 		if (parent_page->frame)
// 		{
// 			success = vm_do_claim_page(child_page);
// 			if (!success)
// 			{
// 				return false;
// 			}
// 			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
// 		}
// 		// 부모 주소로 찾아서 복사
// 	}
// 	// 만약 uninit이 아닌 경우 setup_stack()함수에서 했던 것처럼 페이지를 생성한뒤
// 	// 바로 해당 페이지의 타입에 맞는 initializer를 호출해 페이지 타입을 변경시켜줍니다.
// 	// 그리고 나서 부모페이지의 물리 메모리 정보를 자식에게도 복사해주어야 합니다.

// 	return true;
// }

/* 추가 페이지 테이블에서 리소스 보류 해제 */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// struct hash_iterator i;

	// hash_first(&i, &spt->spt_hash);
	// while (hash_next(&i))
	// {
	// 	struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);

	// 	// if (page->operations->type == VM_FILE){
	// 	// munmap(page->va);
	// 	// }

	// 	hash_destroy(&spt->spt_hash, remove_spt);
	// }
	hash_destroy(&spt->spt_hash, remove_spt);
}

void remove_spt(struct hash_elem *elem, void *aux)
{
	struct page *page = hash_entry(elem, struct page, hash_elem);
	free(page);
}
