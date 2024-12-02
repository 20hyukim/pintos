/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "vm/anon.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
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

struct bitmap *swap_table;
size_t slot_max;

/* Initialize the data for anonymous pages */
/* HDD과 연관되지 않는 anon type에 대해, init을 */
void vm_anon_init(void) {
    /* TODO: Set up the swap_disk. */
    swap_disk = disk_get(1, 1); // 1:1 (스왑공간)에 해당하는 디스크를 반환. ; 즉, 스왑 디스크 반환
    slot_max = disk_size(swap_disk) / SLOT_SIZE; // 디스크 크기와 슬롯 크기를 이용해 사용할 수 있는 스왑 슬롯의 최대 개수를 계산
    swap_table = bitmap_create(slot_max); // 스왑 슬롯 사용여부를 확인하기 위해 비트를 이용. 따라서, slot_max의 크기에 해당하는 bit string 생성 
	                                      // ex) 01000001 이면 1 사용중 / 0 사용 가능
}
/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) { // kva; kernel virtual address
	/* Set up the handler */
	/* <Pseudo>
	 * 페이지가 지금 UNINIT으로 설정되어 있으니까, 이를 페이지 type에 따라서 다르게 설정해 줌. */
	struct uninit_page *uninit = &page->uninit; // page의 union중 하나에서 설정되어 있는 uninit을 가지고 옴.
	memset(uninit, 0, sizeof(struct uninit_page)); // vm에서 페이지를 차지하고 있는 대상 uninit page에 대해서 0으로 초기화.

	page->operations = &anon_ops; // uninit과 관련된 operations에서 anon_ops operation을 설정해 줌.

	struct anon_page *anon_page = &page->anon;// page union에서 UNINIT이 아니라, anon을 가리키도록 설정.
	
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
