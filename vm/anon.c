/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "vm/anon.h"
#include "lib/kernel/bitmap.h"

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
	anon_page->slot = BITMAP_ERROR; // 아직 해당 페이지가 Swap 영역에 저장되지 않았음을 나타냄. 유효한 swap 슬롯이 없음.

	return true;
}

/* Swap in the page by read contents from the swap disk. */
/* 디스크에서 페이지 복원. */
static bool
anon_swap_in (struct page *page, void *kva) {
	/* pseudo code 
	 * anon_page -> slot에 저장되어 있는 슬롯 정보를 가져온다.
	 * 해당 페이지 정보를 mmap을 통해 가져오고,
	 * page - frame간의 링크를 형성한다. */
	struct anon_page *anon_page = &page->anon;
	size_t slot = anon_page->slot;
	size_t sector = slot * SLOT_SIZE;

	if (slot == BITMAP_ERROR || !bitmap_test(swap_table, slot)) // 페이지가 swap 디스크에 저장된 정보가 없는 경우. 슬롯이 사용중이 아닌 경우 - false
		return false;
	
	bitmap_set(swap_table, slot, false); // swap_in이 되었으므로 해당 slot은 사용 중이 아니므로, 이를 bitmap에 표시해 준다.

	for (size_t i = 0; i < SLOT_SIZE; i ++) // SLOT_SIZE는 한 페이지를 저장하는 데 필요한 디스크 섹터 수를 의미한다.
		disk_read(swap_disk, sector + i, kva + DISK_SECTOR_SIZE * i); // 모든 섹터를 읽어와 페이지 전체 데이터를 복원.

	sector = BITMAP_ERROR; // 해당 페이지가 swap out 상태가 아님을 표시.

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	/* pseudo code
	 * swap 영역에서 slot 할당 받기
	 * (slot을 할당 받았다면) 해당 slot에 메모리 넣고, 해당 RAM을 free
	 * (swap disk 초과로 할당 받지 못했다면) 커널 패닉 */
	struct anon_page *anon_page = &page->anon;

	size_t free_idx = bitmap_scan_and_flip(swap_table, 0, 1, false); // 빈 슬롯을 탐색하고 해당 슬롯을 사용 중으로 표시. 

	if (free_idx == BITMAP_ERROR) // swap slot이 없으면 False 반환.
		return false;
	
	size_t sector = free_idx * SLOT_SIZE; // 디스크에 저장할 위치

	for (size_t i = 0; i < SLOT_SIZE; i++)
		disk_write(swap_disk, sector + i, page->va + DISK_SECTOR_SIZE *i); // swap 디스크에 페이지 데이터를 기록.
	
	anon_page->slot = free_idx; // 데이터가 저장된 swap slot에 대한 정보를 저장.

	// page 와 frame간에 링크를 끊고, pml4에서 해당 페이지를 clear
	page->frame->page = NULL;
	page->frame = NULL;
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	/* pseudo
	 * anon이 사용 중인 리소스 해제, page는 caller가 해제할 것이므로 신경 안써도 된다.
	 * anon이 사용 중인 frame, page를 해제*/

	if (page->frame) {
		list_remove(&page->frame->frame_elem); // 리스트에서 해당 frame 제거
		page->frame->page = NULL; // frame이 page를 가리키는 포인터 제거. NULL
		free(page->frame); // 이후, frame을 free
		page->frame = NULL; // page가 frame을 가리키는 포인터 제거. NULL
	}
}
