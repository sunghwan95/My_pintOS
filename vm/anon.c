/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap* swap_table;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	disk_sector_t dsize = disk_size(swap_disk) / (PGSIZE/DISK_SECTOR_SIZE);
	swap_table = bitmap_create(dsize);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	size_t index = anon_page->idx;
	if(!bitmap_test(swap_table,index)){
		return false;
	}
	for(int i = 0; i < (PGSIZE/DISK_SECTOR_SIZE); i++){
		disk_read(swap_disk, index * (PGSIZE/DISK_SECTOR_SIZE) + i, kva + i * DISK_SECTOR_SIZE);
	}
	bitmap_set(swap_table, index, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t index = bitmap_scan(swap_table, 0, 1, false);
	if(index == BITMAP_ERROR){
		return false;
	}
	for(int i = 0; i < (PGSIZE/DISK_SECTOR_SIZE); i++){
		disk_write(swap_disk, index *  (PGSIZE/DISK_SECTOR_SIZE) + i, page->va + i * DISK_SECTOR_SIZE);
	}
	bitmap_set(swap_table, index, true);
	pml4_clear_page(thread_current()->pml4, page->va);
	anon_page->idx = index;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
