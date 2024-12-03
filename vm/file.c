/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/syscall.h"
#include "userprog/process.h"

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
	//anon과 다르게 디스크에 메모리를 할당 받을 필요가 없다. file이라는 HDD 가 있으니까.
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	/* pseudo : 
	 * file_backed와 관련된 operation을 설정
	 * UNINIT에서 FILE_BACKED라는 상태를 명시.
	 */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* do_mmap: aux에 file 정보들을 저장해 두었던 것을 file_page에 옮겨 담아준다.
	aux - UNINIT 페이지일 때, 쓰던 것 */
	struct aux *aux = (struct aux *)page->uninit.aux;
	file_page->file = aux->file;
	file_page->offset = aux->offset;
	file_page->page_read_bytes = aux->page_read_bytes;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	return lazy_load_segment(page, file_page);
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	/* pseudo 
	 * (dirty 유무 확인)
	 * (true) file에 변경사항 저장. dirty하지 않다고 명시
	 * (false) 바로 swap_out 진행. RAM에서 해당 frame 사용 중이지 않다고 명시.*/
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va)) { // dirty인지 확인. 더럽다면 file에 적어두고, dirty이지 않다고 명시
		file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}

	//page와 frame 연관관계 끊기
	page->frame->page = NULL;
	page->frame = NULL;
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if (pml4_is_dirty(thread_current()->pml4, page->va)) { //pml4페이지에서 해당 페이지가 dirty인지 확인.
		file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, false); // file에 수정 사항을 반영 했으므로, dirty = false;
	}

	if (page->frame) {// page와 frame사이에 link를 해제하고, frame 또한 해제한다. page는 caller가 해제할 것이다.
		list_remove(&page->frame->frame_elem);
		page->frame->page = NULL;
		page->frame = NULL;
		free(page->frame);
	}

	pml4_clear_page(thread_current()->pml4, page->va); // pml4에 있던 va도 clear한다.
}

/* Do the mmap / mmap이 매핑 가능 조건을 확인하는 함수였다면, do_mmap은 실제 매핑을 진행하는 함수.*/
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset) {
    lock_acquire(&filesys_lock); // 파일 시스템에 동시 접근하지 않도록 잠금.
    struct file *mfile = file_reopen(file); //reopen을 통해 파일 핸들을 복제해서 독립적으로 파일을 사용할 수 있도록 설정.
    void *ori_addr = addr; // 매핑이 성공했을 때 반환할 원래 주소.
    size_t read_bytes = (length > file_length(mfile)) ? file_length(mfile) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0); // 페이지 단위인지 확인.
    ASSERT(pg_ofs(addr) == 0); // 시작 주소가 페이지 경계인지 확인.
    ASSERT(offset % PGSIZE == 0);

    struct aux *aux;
    while (read_bytes > 0 || zero_bytes > 0) {
		// 페이지 단위로 page_read_bytes와 page_zero_bytes를 사용.
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        aux = (struct aux *)malloc(sizeof(struct aux)); // aux 동적 할당.
        if (!aux)
            goto err;

        aux->file = mfile; // file 정보
        aux->offset = offset; // offset 정보
        aux->page_read_bytes = page_read_bytes; // 바이트 정보

        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux)) { // vm_alloc_page_with_initializer로 페이지 할당.
            goto err;
        }

        read_bytes -= page_read_bytes; // 처리한 바이트 수를 전체 바이트 수에서 빼주기.
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE; // 다음에 읽을 addr를 찾기 위해 PGSIZE를 더해줌.
        offset += page_read_bytes;
    }
    lock_release(&filesys_lock); // acquire했던 lock 놔주기

    return ori_addr; // 할당 성공 시, 주소 반환

err:
    free(aux);
    lock_release(&filesys_lock);
    return NULL; // 실패 시, NULL 반환.
}

/* Do the munmap */
void
	do_munmap (void *addr) {
	/* pseudo
	 * va에 대해서 spt 확인을 통해 dirty bit 확인
	 * (작성된 경우) file에 반영 시키고 반환
	 * (작성 안된 경우) file에 반영 안하고 반환 
	 */

	struct thread *curr = thread_current();
	struct page *page;

	lock_acquire(&filesys_lock);
	while((page = spt_find_page(&curr->spt, addr))) {
		if (page)
			destroy(page); // destory(page)를 한다는 건, 해당 프로세스를 위해 PM에 할당된 메모리 지우기.

		addr += PGSIZE;
	}
	lock_release(&filesys_lock);
}
