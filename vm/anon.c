/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"


#define page_in_disk (PGSIZE/DISK_SECTOR_SIZE)
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
	disk_sector_t dsize = disk_size(swap_disk) / page_in_disk;
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
	if(!bitmap_test(swap_table,index)){ //스왑 테이블에서 해당 인덱스가 사용하는지 확인,
		return false;
	}
	/*
	디스크는 섹터 단위로 데이터 단위를 표시함.
	페이지 사이즈가 섹터 크기보다 월등히 크기 때문에 페이지를 디스크에 저장하기 위해서는 페이지의 크기를 섹터의 크기만큼 나눠서 저장해야함.
	*/
	for(int i = 0; i < page_in_disk; i++){
		// 스왑 디스크에서 페이지를 읽어와서 kva로 복사.
		disk_read(swap_disk, (index * page_in_disk) + i, kva + (i * DISK_SECTOR_SIZE));
	}
	//스왑 테이블에서 해당 안덱스를 사용하지 않음으로 변경.
	bitmap_set(swap_table, index, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// 스왑테이블에서 0부터 검색을시작, 사용중이지 않은 비트를 탐색.
	size_t index = bitmap_scan(swap_table, 0, 1, false);
	if(index == BITMAP_ERROR){
		return false;
	}
	for(int i = 0; i < page_in_disk; i++){
		//스왐 디스크에서 스왑디스크의 오프셋만큼, 물리메모리에 있는 페이지를 가리킨다.
		disk_write(swap_disk, (index * page_in_disk) + i, page->va + (i * DISK_SECTOR_SIZE));
	}
	//해당 인덱스를 사용중으로 변경
	bitmap_set(swap_table, index, true);
	//가상주소와의 매핑 제거.
	pml4_clear_page(thread_current()->pml4, page->va);
	anon_page->idx = index;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
