#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#define TEST_TIMEOUT 15

#include "test_header.h"

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & ~0x1f))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert that the free list with a specified index has the specified number of
 * blocks in it.
 */
void assert_free_list_size(int index, int size) {
    int cnt = 0;
    sf_block *bp = sf_free_list_heads[index].body.links.next;
    while(bp != &sf_free_list_heads[index]) {
	cnt++;
	bp = bp->body.links.next;
    }
    cr_assert_eq(cnt, size, "Free list %d has wrong number of free blocks (exp=%d, found=%d)",
		 index, size, cnt);
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz = sizeof(int);
	int *x = sf_malloc(sz);

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 1);
	assert_free_block_count(1952, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	// We want to allocate up to exactly four pages, so there has to be space
	// for the header and the link pointers.
	void *x = sf_malloc(8092);
	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	void *x = sf_malloc(100281);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(0, 1);
	assert_free_block_count(100288, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 200, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(0, 2);
	assert_free_block_count(224, 1);
	assert_free_block_count(1696, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
	/* void *w = */ sf_malloc(sz_w);
	void *x = sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);
	sf_free(x);

	assert_free_block_count(0, 2);
	assert_free_block_count(544, 1);
	assert_free_block_count(1376, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
        size_t sz_u = 200, sz_v = 300, sz_w = 200, sz_x = 400, sz_y = 200, sz_z = 500;
	void *u = sf_malloc(sz_u);
	/* void *v = */ sf_malloc(sz_v);
	void *w = sf_malloc(sz_w);
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(0, 4);
	assert_free_block_count(224, 3);
	assert_free_block_count(64, 1);

	// First block in list should be the most recently freed block.
	int i = 4;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - 8,
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - 8);
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int), sz_y = 10, sz_x1 = sizeof(int) * 20;
	void *x = sf_malloc(sz_x);
	/* void *y = */ sf_malloc(sz_y);
	x = sf_realloc(x, sz_x1);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 8);
	cr_assert(bp->header & 0x10, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x1f) == 96,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  bp->header & ~0x1f, 96);

	assert_free_block_count(0, 2);
	assert_free_block_count(32, 1);
	assert_free_block_count(1824, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int) * 20, sz_y = sizeof(int) * 16;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char *)y - 8);
	cr_assert(bp->header & 0x10, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x1f) == 96,
		  "Block size (%ld) not what was expected (%ld)!",
		  bp->header & ~0x1f, 96);

	// There should be only one free block.
	assert_free_block_count(0, 1);
	assert_free_block_count(1888, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(double) * 8, sz_y = sizeof(int);
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char *)y - 8);
	cr_assert(bp->header & 0x10, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x1f) == 32,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  bp->header & ~0x1f, 32);

	// After realloc'ing x, we can return a block of size ADJUSTED_BLOCK_SIZE(sz_x) - ADJUSTED_BLOCK_SIZE(sz_y)
	// to the freelist.  This block will go into the main freelist and be coalesced.
	assert_free_block_count(0, 1);
	assert_free_block_count(1952, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE OR MANGLE THESE COMMENTS
//############################################

//Test(sfmm_student_suite, student_test_1, .timeout = TEST_TIMEOUT) {
//}

Test(sfmm_student_suite, student_test_1, .timeout = TEST_TIMEOUT) {
    // catching the wrong misalignment of memalign

    sf_errno = 0;

    // Request an invalid alignment (not a power of two or less than 32)
    void *x = sf_memalign(100, 20); // 20 is not a power of two

    cr_assert_null(x, "x is not NULL, but should be for invalid alignment!");
    cr_assert(sf_errno == EINVAL, "sf_errno is not set to EINVAL!");
}


Test(sfmm_student_suite, student_test_7, .timeout = TEST_TIMEOUT) {
    sf_errno = 0;

    // Request memory with valid alignment of 64 bytes
    void *x = sf_memalign(200, 64);
    cr_assert_not_null(x, "x is NULL, but should not be for valid alignment!");

    // Check if the address is correctly aligned
    cr_assert(((uintptr_t)x & (64 - 1)) == 0, "Returned address is not 64-byte aligned!");

/*
    sf_show_heap();
*/
    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}


Test(sfmm_student_suite, student_test_2, .timeout = TEST_TIMEOUT) { //malloc int, double, char aligned same 32
    // checking realloc functionality
    sf_errno = 0;

    // Allocate a larger block
    void *x = sf_malloc(200);
    cr_assert_not_null(x, "x is NULL!");
    //sf_show_heap();

    // Reallocate to a smaller size
    void *y = sf_realloc(x, 50);
    cr_assert_not_null(y, "y is NULL after realloc!");
    //sf_show_heap();

    cr_assert(x == y, "Realloc to smaller size changed the pointer!");

    sf_block *bp = (sf_block *)((char *)y - 8);
    cr_assert((bp->header & ~0x1f) == 64,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  bp->header & ~0x1f, 64);

	size_t sz = sizeof(int);
	int *z = sf_malloc(sz);

	cr_assert_not_null(z, "z is NULL!");

	*z = 4;

	cr_assert(*z == 4, "sf_malloc failed to give proper space for an int!");

    // Verify free blocks after reallocating
    assert_free_block_count(0, 1);
    assert_free_block_count(1888, 1);
    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_student_suite, student_test_3, .timeout = TEST_TIMEOUT) {
	// checks for coalesce and adding blocks correctly to free list and checking linking of doubly linked circular free lists
    	sf_errno = 0;
	size_t sz = sizeof(int);
	int *x = sf_malloc(sz);

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	size_t siz = sizeof(double);
	int *y = sf_malloc(siz);

	cr_assert_not_null(y, "y is NULL!");

	*x = 8;

	cr_assert(*x == 8, "sf_malloc failed to give proper space for a double!");

	size_t size = sizeof(char);
	int *z = sf_malloc(size);

	cr_assert_not_null(z, "z is NULL!");

	*z = 1;

	sf_free(x); // free the int
	//sf_show_heap(); // check if put in free list

	cr_assert(*z == 1, "sf_malloc failed to give proper space for a char!");

	size_t sizes = sizeof(int);
	int *w = sf_malloc(sizes);

	cr_assert_not_null(w, "y is NULL!");

	*w = 4;

	cr_assert(*w == 4, "sf_malloc failed to give proper space for another int!");

	//sf_show_heap();
	sf_free(w);
	//sf_show_heap();

	assert_free_block_count(32, 1);
	assert_free_block_count(1888, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}


Test(sfmm_student_suite, student_test_8, .timeout = TEST_TIMEOUT) {
    sf_errno = 0;

    void *y = sf_malloc(8);
    (void)y;

    // Allocate memory with alignment of 128 bytes
    void *x = sf_memalign(300, 128);
    cr_assert_not_null(x, "x is NULL!");

    // Check alignment
    cr_assert(((uintptr_t)x & (128 - 1)) == 0, "Returned address is not 128-byte aligned!");
/*
    sf_show_heap();
*/

    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_student_suite, student_test_9, .timeout = TEST_TIMEOUT) {
    sf_errno = 0;

    void *z = sf_malloc(33);
    (void)z;

    // Perform a memalign allocation
    void *x = sf_memalign(1336, 256);
    cr_assert_not_null(x, "x is NULL!");

    // Then perform a normal malloc
    void *y = sf_malloc(12);
    cr_assert_not_null(y, "y is NULL after malloc!");

    //sf_show_heap();

    sf_realloc(y, 67);

    //sf_show_heap();

    cr_assert(((uintptr_t)x & (128 - 1)) == 0, "Returned address is not 128-byte aligned!");
    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_student_suite, student_test_4, .timeout = TEST_TIMEOUT) {
    // calls malloc and free out of order
    sf_errno = 0;

    // Perform multiple malloc requests
    void *a = sf_malloc(100);
    //sf_show_heap();
    void *b = sf_malloc(200);
    //sf_show_heap();
    void *c = sf_malloc(300);
    //sf_show_heap();
    void *d = sf_malloc(400);
    //sf_show_heap();

    cr_assert_not_null(a, "a is NULL!");
    cr_assert_not_null(b, "b is NULL!");
    cr_assert_not_null(c, "c is NULL!");
    cr_assert_not_null(d, "d is NULL!");

    // Free out of order
    sf_free(b);
    //sf_show_heap();
    sf_free(d);
    //sf_show_heap();
    sf_free(a);
    //sf_show_heap();
    sf_free(c);
    //sf_show_heap();

    // After freeing, check that blocks were coalesced correctly
    assert_free_block_count(0, 1);
    assert_free_block_count(1984, 1);  // All blocks coalesced back together

    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_student_suite, student_test_5, .timeout = TEST_TIMEOUT) {
    // this fills the free list, and then frees all of them out of order to see if they are coalesced into one and it grows a page of memory
    sf_errno = 0;

    // Perform malloc requests to fill free list
    void *x1 = sf_malloc(50);
    //sf_show_heap();
    void *x2 = sf_malloc(100);
    //sf_show_heap();
    void *x3 = sf_malloc(200);
    //sf_show_heap();
    void *x4 = sf_malloc(4061); // grows heap by a page
    //sf_show_heap();
    void *x5 = sf_malloc(50);
    //sf_show_heap();

    cr_assert_not_null(x1, "x1 is NULL!");
    cr_assert_not_null(x2, "x2 is NULL!");
    cr_assert_not_null(x3, "x3 is NULL!");
    cr_assert_not_null(x4, "x4 is NULL!");
    cr_assert_not_null(x5, "x5 is NULL!");

    // Free in a specific order
    sf_free(x3);  // Free middle-sized block first
    //sf_show_heap();
    sf_free(x1);  // Free smallest block next
    //sf_show_heap();
    sf_free(x5);  // Free another small block
    //sf_show_heap();
    sf_free(x4);  // Free the largest block last
    //sf_show_heap();
    sf_free(x2);  // Free medium-sized block last
    //sf_show_heap();

    // Check if blocks were added to the free list correctly
    assert_free_block_count(0, 1);
    assert_free_block_count(6080, 1);  // Check if all blocks coalesced into one

    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_student_suite, student_test_6, .timeout = TEST_TIMEOUT) {
    // mixed free and malloc calls
    sf_errno = 0;

    // Multiple mallocs of different sizes
    void *p1 = sf_malloc(32);
    //sf_show_heap();
    void *p2 = sf_malloc(200);
    //sf_show_heap();

    sf_free(p2);
    //sf_show_heap();

    void *p3 = sf_malloc(32);
    //sf_show_heap();

    sf_free(p1);
    //sf_show_heap();

    void *p4 = sf_malloc(40);
    //sf_show_heap();
    void *p5 = sf_malloc(32);
    //sf_show_heap();

    void *p6 = sf_malloc(32);
    //sf_show_heap();

    sf_free(p3);
    //sf_show_heap();

    sf_free(p6);
    //sf_show_heap();

    cr_assert_not_null(p1, "p1 is NULL!");
    cr_assert_not_null(p2, "p2 is NULL!");
    cr_assert_not_null(p3, "p3 is NULL!");
    cr_assert_not_null(p4, "p4 is NULL!");
    cr_assert_not_null(p5, "p5 is NULL!");
    cr_assert_not_null(p6, "p6 is NULL!");

    // Free in reverse order to stress the free list
    sf_free(p1);
    //sf_show_heap();
    sf_free(p5);
    //sf_show_heap();

    // Check the free list status after freeing
    assert_free_block_count(0, 1);
    assert_free_block_count(1984, 1);  // After freeing, all should coalesce into a single block

    cr_assert(sf_errno == 0, "sf_errno is not zero!");
}