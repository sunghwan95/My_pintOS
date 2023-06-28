/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"

static bool lazy_mmap(struct page *page, void *aux);

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	struct segment *seg = (struct segment*)page->uninit.aux;
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	struct file *file = file_page->file;
	off_t offset = file_page->offset;
	size_t read_bytes = file_page->read_bytes;
	size_t zero_bytes = PGSIZE - read_bytes;

	if (file_read_at(file, kva, read_bytes, offset) != (int)read_bytes){
		return false;
	}

	memset(kva + read_bytes, 0, zero_bytes);
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	struct segment *seg = (struct segment*)page->uninit.aux;
	file_page->offset = seg->ofs;
	file_page->file = seg->file;
	file_page->read_bytes = seg->page_read_bytes;

	if (pml4_is_dirty(thread_current()->pml4, page->va)){
		lock_acquire(&filesys_lock);
		file_write_at(seg->file, page->va, seg->page_read_bytes, seg->ofs);
		lock_release(&filesys_lock);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}
	pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	free(page->frame);
}

/* Do the mmap */
//내 꺼
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	void* init_addr = addr;
	struct file *r_file = file_reopen(file);

	size_t file_size = (size_t)file_length(r_file);
	size_t read_bytes = file_size >= length ? length : file_size;
	size_t zero_bytes = (PGSIZE - (read_bytes % PGSIZE)) % PGSIZE;

	while(read_bytes > 0 || zero_bytes > 0){
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct segment *seg = (struct segment*)malloc(sizeof(struct segment));
		seg->file = r_file;
		seg->page_read_bytes = page_read_bytes;
		seg->ofs = offset;
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_mmap, seg)){
			free(seg);
			return false;
		}
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return init_addr;
}

void do_munmap(void *addr){
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, addr);
	if (page == NULL)
		return; 

	while (page != NULL){
		struct segment *seg = (struct segment*)page->uninit.aux;
		if (pml4_is_dirty(curr->pml4, addr)){
			lock_acquire(&filesys_lock);
			file_write_at(seg->file, addr, seg->page_read_bytes, seg->ofs);
			lock_release(&filesys_lock);
			pml4_set_dirty(curr->pml4, addr, 0);
		}
		pml4_clear_page(curr->pml4, page->va);
		addr += PGSIZE;
		page = spt_find_page(&curr->spt, addr);
	}
}

static bool lazy_mmap(struct page *page, void *aux){
	struct frame *frame = page->frame;
	struct segment *seg = (struct segment*)aux;

	struct file *file = seg->file;
	off_t offset = seg->ofs;
	size_t page_read_bytes = seg->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	uint8_t *kva = page->frame->kva;
	if(kva == NULL){
		free(page);
		return false;
	}

	if(file_read_at(file, frame->kva, page_read_bytes, offset) != (int)page_read_bytes){
		free(page);
		free(seg);
		return false;
	}

	memset(frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}
