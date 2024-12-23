/**
* Do not submit your assignment with a main function in this file.
* If you submit with a main function in this file, you will get a zero.
*/

#include "sfmm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "debug.h"
#include "errno.h"

#include "test_header.h"

size_t align_size(size_t size) {
    size_t size_plus_header = size + sizeof(sf_header);
    size_t aligned_size = (size_plus_header+ MIN_BLOCK_SIZE - 1) & ~(MIN_BLOCK_SIZE - 1);
    return aligned_size;
}

size_t get_block_size(sf_block *block_ptr) {
    size_t header_value = block_ptr->header;
    return header_value & (~0x1F);
    // return ((block_ptr->header >> 5) << 5);
}
int set_block_size(sf_block *block, size_t size) {
    size_t aligned_size = size & (~0x1F);
    block->header = (block->header & 0x1F) | aligned_size; // Preserve the lower 3 bits
    return 0;
}

int set_prev_alloc_bit(sf_block *block, int flag) {
    if (flag != 0) {
        block->header |= PREV_BLOCK_ALLOC;
    } else { // prev alloc is 0
        block->header &= ~PREV_BLOCK_ALLOC;
    }
    return 0;
}
int get_prev_alloc_bit(sf_block *block) {
    return (block->header & PREV_BLOCK_ALLOC) >> 1;
}

int set_curr_alloc_bit(sf_block *block, int flag) {
    if (flag != 0) {
        block->header |= CURR_BLOCK_ALLOC;
    } else { // alloc is 0
        block->header &= ~CURR_BLOCK_ALLOC;
    }
    return 0;
}
int get_curr_alloc_bit(sf_block *block) {
    return (block->header & CURR_BLOCK_ALLOC);
}

sf_block *get_block_end(sf_block *block) {
    return (sf_block *)((void *)block + get_block_size(block));
}

sf_footer *write_footer_only_free_blocks(sf_block *block) {

    // Get the block size (including header and footer)
    size_t size = get_block_size(block);

    // Calculate the footer location: end of the block, just before the next block's header
    sf_footer *footer = (sf_footer *)((char *)block + size - sizeof(sf_footer));

    // Copy the header to the footer (free blocks only)
    *footer = (sf_footer)block->header;

    return footer;
}

sf_block *write_block_header(sf_block *block, size_t size, int prev_alloc, int alloc) {

    // Set the block size in the header, ensuring the lower 3 bits are zero (alignment requirement)
    block->header = size;

    // Set the allocation and previous allocation bits using bitwise operations
    set_curr_alloc_bit(block, alloc);

    set_prev_alloc_bit(block, prev_alloc);

    // If the block is being set as free, write the footer
    if (alloc == 0) {
        write_footer_only_free_blocks(block);
    }

    return block;
}

/*
    THIS FUNCTION IS STRICTLY FOR ACCESSING AN EXACT MATCH IN THE FREE LIST TRAVERSAL
    THEREFORE IT RETURNS THE EXACT MATCH BLOCK THAT IT HAS FOUND
    Return the Block: The function returns the block, which is essential for using it to satisfy a memory allocation request.
    Handling Circular List: If the block is the only one in the free list, the sentinel node is reset to point to itself, keeping the list circular.
    Unlinking the Block: The block is unlinked from the list by updating the next and prev pointers of neighboring blocks.
    Clearing Links: After unlinking, the block’s next and prev pointers are set to NULL to indicate that it's no longer part of any list, making it ready for allocation.
*/
sf_block *unlink_block_from_free_list_return_malloc_request(sf_block *block) {

    // 1. Check if the block is allocated or its links are invalid
    if (get_curr_alloc_bit(block)) { // curr bit is 1
        return NULL;
    }
    if (block->body.links.next == NULL || block->body.links.prev == NULL) {
        return NULL; // Block is either allocated or pointers are invalid, return NULL
    }

    // 2. Get the next and previous blocks in the free list
    sf_block *next_block = block->body.links.next;
    sf_block *prev_block = block->body.links.prev;

    // 3. Handle the case where the block is the only one in the list (circular list)
    if (next_block == block && prev_block == block) {
        // This is the only block in the list, reset the sentinel to point to itself
        // This is the only block in the list, reset the sentinel to point to itself
        // Access the sentinel node directly using the known structure
        sf_block *sentinel = &sf_free_list_heads[(block->header & ~0x1F) >> 5]; // Example size class calculation
        sentinel->body.links.next = sentinel;
        sentinel->body.links.prev = sentinel;
    } else {
    // 4. Relink the previous and next blocks to bypass the current block
        prev_block->body.links.next = next_block;
        next_block->body.links.prev = prev_block;
    }

    // 5. Clear the block's links to indicate it's no longer in the free list
    block->body.links.next = NULL;
    block->body.links.prev = NULL;

    // 6. Return the unlinked block, ready to be allocated
    return block;
}

void initialize_free_lists(int index) {
// Base case: If initialized all free lists, stop recursion.
    if (index >= NUM_FREE_LISTS) {
        return;
    }
// Initialize the current sentinel to point to itself.
    sf_free_list_heads[index].body.links.next = &sf_free_list_heads[index];
    sf_free_list_heads[index].body.links.prev = &sf_free_list_heads[index];
// Recurse to initialize the next sentinel.
    initialize_free_lists(index + 1);
}

int get_free_list_index(size_t size) {
// Define Fibonacci-based size classes for the free lists
    size_t fib_sizes[NUM_FREE_LISTS] = {MIN_BLOCK_SIZE, 2 * MIN_BLOCK_SIZE, 3 * MIN_BLOCK_SIZE,
    5 * MIN_BLOCK_SIZE, 8 * MIN_BLOCK_SIZE, 13 * MIN_BLOCK_SIZE,
    21 * MIN_BLOCK_SIZE, 34 * MIN_BLOCK_SIZE};

// Iterate through the size classes and return the corresponding index
    for (int index = 0; index < NUM_FREE_LISTS - 1; index++) {
        if (size <= fib_sizes[index]) {
            return index;
        }
    }

// If the size is larger than the largest Fibonacci class, return the last index
    return NUM_FREE_LISTS - 1;
}

sf_block *get_free_list_head_to_search_for_block(size_t size) {
    // THIS FUNCTION IS FOR FINDING THE HEAD OF EACH FREE LIST, AND RETURNING THE HEAD OF THE FREE LIST THAT SHOULD BE SEARCHED
    // IT RETURNS THE HEAD OF THE FREE LIST THAT IS LIKELY TO CONTAIN THE CORRECT SIZE BLOCKS TO SATISFY THE REQUESTS

    // Define Fibonacci-based size classes for the free lists
    size_t fib_sizes[NUM_FREE_LISTS] = {MIN_BLOCK_SIZE, 2 * MIN_BLOCK_SIZE, 3 * MIN_BLOCK_SIZE,
    5 * MIN_BLOCK_SIZE, 8 * MIN_BLOCK_SIZE, 13 * MIN_BLOCK_SIZE,
    21 * MIN_BLOCK_SIZE, 34 * MIN_BLOCK_SIZE};

    // Iterate through the size classes and return the corresponding index
    for (int index = 0; index < NUM_FREE_LISTS - 1; index++) {
        if (size <= fib_sizes[index]) {
            return &sf_free_list_heads[index];
        }
    }

    // If the size is larger than the largest Fibonacci class, return the last index
    return &sf_free_list_heads[NUM_FREE_LISTS - 1];
}

sf_block *find_and_remove_exact_match_free_list_block(sf_block *free_list_head_pntr, size_t size) {
    // THIS IS FOR FINDING THE EXACT MATCH OF A BLOCK IN A FREE LIST: CASE 1 - BEST CASE WHICH IS EXACT MATCH

    sf_block *free_list_iteration = free_list_head_pntr->body.links.next;

    // Traverse the circular doubly linked list
    while (free_list_iteration != free_list_head_pntr) {
        if (get_block_size(free_list_iteration) == size) {
            free_list_iteration = unlink_block_from_free_list_return_malloc_request(free_list_iteration); // IF AN EXACT MATCH IS FOUND IT IS UNLINKED AND RETURNED
            return free_list_iteration; // Return the block found and removed
        }
        free_list_iteration = free_list_iteration->body.links.next; // Move to the next block
    }

    // No block of the desired size was found
    return NULL;
}

/*
    Coalescing with the Next Block:
    Locates the next block using the size in the current block’s header.
    Combines the sizes of the current and next blocks.
    Updates the header and footer of the coalesced block.

    1. Locating the Next Block:
    Step 1: The size of the current block (after possibly coalescing with the previous block) is stored in its header: block->header & ~0x1F.
    Step 2: The next block is located by adding the size of the current block to its starting address: next_block = (sf_block *)((char *)block + (block->header & ~1F)).

    2. Combining the Current Block with the Next Block:
    Step 3: The allocation status of the next block is checked using the THIS_BLOCK_ALLOC flag (next_block->header & 0x1).
    Step 4: If the next block is free (i.e., the allocation flag is not set), the current block is coalesced with the next block by adding the size of the next block to the current block’s size: block->header += next_block->header & ~0x1F.
    
    3. Updating the Header and Footer:
    Step 5: The footer of the coalesced block is updated to reflect the combined size. It is located at the end of the newly coalesced block: sf_footer *next_footer = (sf_footer *)((char *)next_block + (next_block->header & ~0x1F) - sizeof(sf_footer)). The footer is set to the updated header value: *next_footer = block->header.
    Step 6: The header of the coalesced block is also updated to reflect the new, larger size.
*/
sf_block *coalesce_next(sf_block *block) {
    // Locate the next block using the block end address
    sf_block *next_block = get_block_end(block);

    // Check if the current block is allocated; if so, no coalescing
    if (get_curr_alloc_bit(block)) { // curr bit is 1
        return NULL;
    } else {
        set_prev_alloc_bit(next_block, 0); // there is the current block, before the next one that has been checked to be not allocated
    }

    // Unlink the previous block from the free list
    // Removing the block from the free list
    if (get_curr_alloc_bit(next_block)) { // curr bit is 1
        return NULL;
    }
    if (next_block->body.links.next == NULL) {
        return NULL;
    }
    if (next_block->body.links.prev == NULL) {
        return NULL;
    }
    sf_block *next = next_block->body.links.next;
    sf_block *prev = next_block->body.links.prev;
    next->body.links.prev = prev;
    prev->body.links.next = next;

    // Ensure the next block can be coalesced (it should not be allocated)
    if (get_prev_alloc_bit(next_block)|| get_curr_alloc_bit(next_block)) {
        return NULL;
    }

    // Combine the current block with the next block
    size_t new_size = get_block_size(block) + get_block_size(next_block);
    int prev_bit = 0;
    if (get_prev_alloc_bit(block)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    // Update the current block's header to reflect the new coalesced size
    // Write header with size and allocation flags
    write_block_header(block, new_size, prev_bit, 0);

    sf_block *next_free_block = next_block->body.links.next;
    sf_block *prev_free_block = next_block->body.links.prev;
    // Set next and previous links for free block
    block->body.links.next = next_free_block;
    block->body.links.prev = prev_free_block;

    return block; // Return the coalesced block
}

/*
    Coalescing with the Previous Block:
    Locates the previous block by using the footer of the previous block.
    Combines the size of the current and previous blocks.
    Updates the header and footer of the coalesced block.

    1. Locating the Previous Block:
    Step 1: The PREV_BLOCK_ALLOC flag (bit 1 in the header, checked using block->header & 0x8) indicates whether the previous block is free or allocated.
    Step 2: If this flag is not set (i.e., !(block->header & 0x8)), it means the previous block is free.
    Step 3: To locate the previous block, the footer of the previous block is found at block - sizeof(sf_footer) (since the footer is at the end of the previous block).
    Step 4: From the footer, the size of the previous block is retrieved using prev_block_size = *prev_footer & ~0x1F. The ~0x1F mask clears the lower 3 bits used for the allocation status and alignment flags, leaving the size.
    Step 5: Using the size, the start of the previous block is found: prev_block = (sf_block *)((char *)block - prev_block_size).

    2. Combining the Current Block with the Previous Block:
    Step 6: Once the previous block is located, its header is updated by adding the size of the current block to it: prev_block->header += block->header & ~0x1F. This updates the size of the previous block to include both the previous block and the current block.
    Step 7: The current block is now part of the previous block, so the reference coalesced_block is updated to point to prev_block.
    3. Updating the Header and Footer:
    Step 8: The footer of the coalesced block (i.e., the original block’s footer) is now moved to the end of the new coalesced block. The footer is set to the updated header value: *footer = block->header.

    Note: If the block was part of a free list, it should be removed from the free list before coalescing and then reinserted into the appropriate free list after coalescing is completed.
          remove from the free list before coalescing to ensure that the free list doesn't contain stale references to blocks.
*/
sf_block *coalesce_prev(sf_block *block) {
    // Check if the previous block is allocated; if so, no coalescing
    if (get_prev_alloc_bit(block) || get_curr_alloc_bit(block)) {
        return NULL; //  don't coalesce if the previous block is allocated or the current block is allocated
        // only should perform coalesce if blocks are free - this is to prevent fragmentation
    }

    // Locate the previous block using its footer
    sf_footer *prev_footer = (sf_footer *)((char *)block - sizeof(sf_footer));
    size_t prev_block_size = *prev_footer & ~0x1F;  // Get previous block's size (masking out flags)

    // Locate the previous block using its size
    sf_block *prev_block = (sf_block *)((void *)prev_footer - prev_block_size + sizeof(sf_footer));

    // If the previous block is allocated, return without coalescing
    if (get_curr_alloc_bit(prev_block)) {
        set_prev_alloc_bit(block, 1);
        return NULL;
    }

    // Unlink the previous block from the free list
    // Removing the block from the free list
    if (get_curr_alloc_bit(prev_block)) {
        return NULL;
    }
    if (prev_block->body.links.next == NULL) {
        return NULL;
    }
    if (prev_block->body.links.prev == NULL) {
        return NULL;
    }

    sf_block *next = prev_block->body.links.next;
    sf_block *prev = prev_block->body.links.prev;
    next->body.links.prev = prev;
    prev->body.links.next = next;
    prev_block->body.links.next = NULL;
    prev_block->body.links.prev = NULL;

    // Combine the two blocks
    size_t new_size = get_block_size(prev_block) + get_block_size(block);
    int prev_bit = 0;
    if (get_prev_alloc_bit(prev_block)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    // Update the previous block's header to reflect the new coalesced size
    // Write header with size and allocation flags
    write_block_header(prev_block, new_size, prev_bit, 0);

    sf_block *next_free_block = 0;
    sf_block *prev_free_block = 0;
    // Set next and previous links for free block
    prev_block->body.links.next = next_free_block;
    prev_block->body.links.prev = prev_free_block;

    return prev_block; // Return the coalesced block
}

/*
    General Coalesce Idea:
    Locate the Previous Block: Use the footer of the current block to find the previous block.
    Coalesce with the Previous Block: If free, update the header of the previous block and extend the block size to include the current block.
    Locate the Next Block: Use the size in the current block's header to find the next block.
    Coalesce with the Next Block: If free, update the header to reflect the combined size of the current and next blocks.
    Update Headers and Footers: Ensure both header and footer of the coalesced block are correctly set with the new combined size.
*/
sf_block *coalesce(sf_block *block) {

    sf_block *coalesced_block = block;

    // Check and coalesce with the previous block
    // 1. Coalescing with the previous block
    sf_block *prev_block = coalesce_prev(coalesced_block);
    while (prev_block != NULL) {
        coalesced_block = prev_block;
        prev_block = coalesce_prev(coalesced_block);
    }

    // Check and coalesce with the next block
    // 2. Coalescing with the next block
    sf_block *next_block = coalesce_next(coalesced_block);
    while (next_block != NULL) {
        coalesced_block = next_block;
        next_block = coalesce_next(coalesced_block);
    }

    return coalesced_block;
}

sf_block *coalesce_if_possible(sf_block *block) {
    sf_block *coalesced_block = coalesce(block);
    return (coalesced_block != NULL) ? coalesced_block : block;
}

/*
    The assignment specifies that free lists should be managed using a last-in, first-out (LIFO) discipline. 
    This means that newly freed blocks should be inserted at the front of the corresponding free list
    Helper function to insert a block into the free list using LIFO discipline
*/
void insert_block_to_free_list(sf_block *block) {
    // Get the size of the block and find the appropriate free list
    size_t block_size = get_block_size(block);

    sf_block *free_list_head = get_free_list_head_to_search_for_block(block_size);

    // Insert the block at the front of the free list
    sf_block *next = free_list_head->body.links.next;

    block->body.links.next = next;
    block->body.links.prev = next->body.links.prev;
    next->body.links.prev->body.links.next = block;
    next->body.links.prev = block;
}

/*
    Initial Validation: Checks if the block is NULL, allocated, or smaller than the minimum size.
    Coalescing: Coalesces with adjacent free blocks if possible.
    Block Size Retrieval: Gets the block's size and determines the appropriate free list using get_free_list_head_address.
    Header/Footer Update: Clears the allocated bit in the header and sets the footer to match the header.
    Insertion in Free List: Inserts the block at the front of the free list (LIFO policy).
    Return: Returns the block to indicate successful addition.
*/
sf_block *add_block_free_list_LIFO(sf_block *block) {
    // Step 1: Validate the block
    if (block == NULL || get_curr_alloc_bit(block) || get_block_size(block) < MIN_BLOCK_SIZE) {
        return NULL;  // Invalid block
    }

    // Step 2: Attempt to coalesce the block
    block = coalesce_if_possible(block);

    // Step 3: Insert the block into the appropriate free list
    insert_block_to_free_list(block);

    return block;
}

sf_block *split_free_block(sf_block *free_block_part_remaining_put_back, sf_block *allocated_block_to_return, size_t requested_size_plus_minBlock) {
    // Ensure the block is free and large enough to split
    if (get_curr_alloc_bit(free_block_part_remaining_put_back)) {
        return NULL; // Block is allocated, cannot split
    }

    size_t block_size = get_block_size(free_block_part_remaining_put_back);
    if (block_size < requested_size_plus_minBlock) {
        return NULL; // Splitting would create a splinter
    }

    size_t requested_size = requested_size_plus_minBlock - MIN_BLOCK_SIZE;
    // Calculate remaining size after splitting
    size_t remaining_size = block_size - requested_size; // remove the size of the epilogue

    int prev_bit = 0;
    if (get_prev_alloc_bit(free_block_part_remaining_put_back)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    // Write header for the allocated block
    write_block_header(allocated_block_to_return, requested_size, prev_bit, 1);

    // Write a new free block in the remaining space (if large enough)
    if (remaining_size >= MIN_BLOCK_SIZE) {
        sf_block *new_free_block = (sf_block *)((char *)free_block_part_remaining_put_back + requested_size);
        write_block_header(new_free_block, remaining_size, 1, 0);

        // If the remaining block is valid, add it back to the free list
        if (new_free_block != NULL) {
            add_block_free_list_LIFO(new_free_block);
        }
    }

    return allocated_block_to_return; // No free block to return if the remaining size was too small
}

/*
    Function to remove a block from the free list
    Breakdown:
    Input:
    Takes a pointer to the block that needs to be removed from the free list.

    Process:
    Each block in a free list is part of a doubly linked circular list, where each block points to the next and previous blocks (next and prev pointers).
    The function adjusts the prev block's next pointer and the next block's prev pointer to "skip" over the block being removed.
    This effectively removes the block from the circular list.

    Nullify Pointers:
    After the block is removed, the block's next and prev pointers are set to NULL to signify that it's no longer part of the list.

    The function only modifies the pointers in the list and doesn't need to return anything. 
    It ensures the list is correctly maintained by unlinking the block from the free list.
*/
void remove_from_free_list(sf_block *block) {
    block->body.links.prev->body.links.next = block->body.links.next;
    block->body.links.next->body.links.prev = block->body.links.prev;

    // Set the block's next and prev pointers to NULL to signify it's removed
    block->body.links.next = NULL;
    block->body.links.prev = NULL;
}

// Helper function to allocate a block when can split the block
sf_block *allocate_block_with_split_from_free(sf_block *split_part_satisfy_malloc_request, sf_block *free_block_to_split, size_t size) {

    // Remove the block from the free list
    remove_from_free_list(free_block_to_split);

    // Split the block and get back the remaining free block that was split off
    split_part_satisfy_malloc_request = split_free_block(free_block_to_split, split_part_satisfy_malloc_request, size);

    return split_part_satisfy_malloc_request; // Return the allocated block
}

// Helper function to allocate a block when no split can be made without creating a splinter
sf_block *allocate_block_waste_space(sf_block *free_block_to_return, size_t size) {
    // Remove the block from the free list
    remove_from_free_list(free_block_to_return);

    int prev_bit = 0;
    if (get_prev_alloc_bit(free_block_to_return)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    // Write header for the allocated block
    write_block_header(free_block_to_return, size, prev_bit, 1);

    return free_block_to_return;
}

/*
    THIS IS STRICTLY FOR FINDING A BLOCK THAT IS AT LEAST AS BIG AS THE MALLOC REQUEST SIZE: CASE 3
    This is where no block is big enough to split and not create a splinter (where the remaining piece is still greater than minimum block size 32)
    Therefore, before the absolute worst case (where we have to grow the heap and add more pages of memory) it just looks for a block that is big enough
    There will be fragmentation and wasted space at the "end of the block" where it is not used for the allocation request, but can't be split away

    Iterate through all free lists but since can't split without making splinter
    Instead just find a block that is at least as big, and use without splitting ust waste the space
*/
sf_block *find_and_allocate_block_no_split_waste_space(int free_list_index_matched, size_t size) {
    for (int current_list = free_list_index_matched; current_list < NUM_FREE_LISTS; current_list++) {
        sf_block *pntr_free_list_head = &sf_free_list_heads[current_list];
        sf_block *access_free_list = pntr_free_list_head->body.links.next;

        // Traverse the free list to find a suitable block
        while (access_free_list != pntr_free_list_head) {
            // Check if the block is large enough
            if (get_block_size(access_free_list) >= size) {
                return allocate_block_waste_space(access_free_list, size);
            }
            access_free_list = access_free_list->body.links.next; // Move to the next block
        }
    }

    return NULL; // No suitable block found
}

/*
    THIS IS STRICTLY FOR FINDING A BLOCK THAT IS BIG ENOUGH TO SPLIT SO ONE PIECE SATISFIES THE MALLOC REQUEST AND THE OTHER IS > 32
    SPLITS BLOCK TO COVER MALLOC REQUEST WITHOUT CREATING A SPLINTER
*/
sf_block *find_and_allocate_block_split_no_splinter(int free_list_index_matched, size_t size) {
    // Iterate through all free lists and try and split with no splinter
    // the requested size >= 32 is matched to a block, and that block is split where the splitted piece is also >= 32
    for (int current_list = free_list_index_matched; current_list < NUM_FREE_LISTS; current_list++) {
        sf_block *pntr_free_list_head = &sf_free_list_heads[current_list];
        sf_block *access_free_list = pntr_free_list_head->body.links.next;

        // Traverse the free list to find a suitable block
        while (access_free_list != pntr_free_list_head) {
            // Check if the block is large enough
            if (get_block_size(access_free_list) >= size) { // size is already passed as parameter with + 32 so already accounting for split
                sf_block *split_part_satisfy_malloc_request = pntr_free_list_head->body.links.next;
                // Allocate the block
                return allocate_block_with_split_from_free(split_part_satisfy_malloc_request, access_free_list, size);
            }
            access_free_list = access_free_list->body.links.next; // Move to the next block
        }
    }
    return NULL; // No suitable block found
}

void *padding(void *startAddr) {
    // Adjust for 32-byte alignment (either 8 or 24 padding)
    if ((((uintptr_t)startAddr) & 0x1F) != 0) {
        startAddr += 8;
    } else {
        startAddr += 24;
    }
    return startAddr;
}

/*
    Get more memory for the heap
    Turn it into a new block (set size so space for epilogue)
    Insert the new block into the heap (would be combined by coalesce together)
*/
sf_block *grow_heap() {
    sf_block *original_epilogue = sf_mem_end() - sizeof(sf_footer); // this will be overwritten and made into the new header
    int prev_bit = 0;
    if (get_prev_alloc_bit(original_epilogue)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    sf_block *new_mem_block = original_epilogue; // create the new block of memory right where the old memory used to end
    void *old_memory_end = sf_mem_end();

    void *heap_grow = sf_mem_grow(); // grow the heap by one page
    if (heap_grow == NULL) {
        sf_errno = ENOMEM;
        //fprintf(stderr, "ERROR: growing the heap, sf_errno set\n");
        return NULL;
    }

    void *new_epilogue = sf_mem_end() - EPILOGUE_SIZE; // set up the new epilogue repositioned at the end of the heap
    write_block_header(new_epilogue, 0, 0, 1); // write the epilogue information

    int mem_size = sf_mem_end() - old_memory_end; // set up the new block of memory
    write_block_header(original_epilogue, mem_size, prev_bit, 0);
    original_epilogue->body.links.next = 0;
    original_epilogue->body.links.prev = 0;

    sf_block *coalesced_mem_block = coalesce(new_mem_block); // combine the pages of memory
    add_block_free_list_LIFO(coalesced_mem_block); // add new block of memory back to free list

/*
    fprintf(stderr, "The heap has been grown! \n");
    sf_show_heap();
    fprintf(stderr, "\n");
*/

    return coalesced_mem_block;
}

/*
    THIS IS THE MAIN DRIVER CODE OF LOGIC THAT TRAVERSES THE FREE LIST AND LOOKS FOR A BLOCK
*/
sf_block *get_free_list_block(size_t size) {
    sf_block *free_list_head_pntr = get_free_list_head_to_search_for_block(size);
    if (free_list_head_pntr == NULL) {
        initialize_free_lists(0);
        return NULL;
    }

    sf_block *free_list_iteration = find_and_remove_exact_match_free_list_block(free_list_head_pntr, size);
    if (free_list_iteration != NULL) {
        return free_list_iteration;
    }

    int free_list_index_matched = get_free_list_index(size + MIN_BLOCK_SIZE);

    sf_block *satisfying_block_with_split = find_and_allocate_block_split_no_splinter(free_list_index_matched, size + MIN_BLOCK_SIZE);
    if (satisfying_block_with_split != NULL) {
        return satisfying_block_with_split;
    }

    sf_block *satisfying_block_with_waste_space = find_and_allocate_block_no_split_waste_space(free_list_index_matched, size);
    if (satisfying_block_with_waste_space != NULL) {
        return satisfying_block_with_waste_space;
    }

    return NULL;
}

int init_heap() {
    // Grow the heap by one page
    sf_block *heap_start = sf_mem_grow();
    if (heap_start == NULL) {
        //fprintf(stderr, "ERROR: initializing heap, sf_errno set\n");
        sf_errno = ENOMEM;
        return 0;
    }

    void *startAddr = sf_mem_start();

    startAddr = padding(startAddr);

    // Initialize the prologue directly in the heap memory
    sf_block *prologue = (sf_block *)startAddr;
    set_block_size(prologue, PROLOGUE_SIZE + 16 + 8); // Including header and padding
    set_curr_alloc_bit(prologue, 1);
    set_prev_alloc_bit(prologue, 0);

    // Set up the epilogue in-place
    void *endAddr = sf_mem_end() - EPILOGUE_SIZE;
    sf_block *epilogue = (sf_block *)endAddr;
    epilogue->header = (size_t)16;  // Only the header with block size 0 and allocated bit

    // Initialize the wilderness block in between prologue and epilogue
    size_t size_block = (sf_mem_end() - EPILOGUE_SIZE) - (startAddr + PROLOGUE_SIZE) + EPILOGUE_SIZE;
    sf_block *firstBlock = (sf_block *)(startAddr + PROLOGUE_SIZE);
    firstBlock->header = size_block;

    firstBlock->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS - 1];
    firstBlock->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS - 1];

    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next = firstBlock;
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.prev = firstBlock;

    // Set footer identical to header for the free block
    sf_block *footer = (sf_block *)((char *)endAddr - EPILOGUE_SIZE);
    footer->header = size_block;

/*
    fprintf(stderr, "The heap has been initialized! \n");
    sf_show_heap();
    fprintf(stderr, "\n");
*/

    return 1;
}

int check_initialized_heap() {
    if (sf_mem_start() == sf_mem_end()) {
        initialize_free_lists(0);

        if (init_heap() == 0) return 0;
    }
    return 1;
}

sf_block *create_allocated_block(sf_block *free_list_block_ret) {
    size_t size_block_ret = get_block_size(free_list_block_ret);
    int prev_bit = 0;
    if (get_prev_alloc_bit(free_list_block_ret)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    write_block_header(free_list_block_ret, size_block_ret, prev_bit, 1);
    set_prev_alloc_bit(get_block_end(free_list_block_ret), 1);
/*
    sf_show_block(free_list_block_ret);
    fprintf(stderr, "\n");
*/
    return free_list_block_ret;
}

/*
 * This is your implementation of sf_malloc. It acquires uninitialized memory that
 * is aligned and padded properly for the underlying system.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If size is 0, then NULL is returned without setting sf_errno.
 * If size is nonzero, then if the allocation is successful a pointer to a valid region of
 * memory of the requested size is returned.  If the allocation is not successful, then
 * NULL is returned and sf_errno is set to ENOMEM.
 */
void *sf_malloc(size_t size) {
    sf_errno = 0;

    if (size == 0) return NULL;

    if (!check_initialized_heap()) return NULL;

    size_t size_align = align_size(size);
    if (!size_align) return NULL;

    sf_block *free_list_block_ret = get_free_list_block(size_align);
    while (free_list_block_ret == NULL) {
        sf_block *more_memory = grow_heap();
        if (!more_memory) { // no more memory can be added
            //fprintf(stderr, "ERROR: growing the heap, sf_errno set\n");
            sf_errno = ENOMEM;
            return NULL;
        }
        free_list_block_ret = get_free_list_block(size_align); // try to find allocate space in free list again
    }

    sf_block *allocated_block = create_allocated_block(free_list_block_ret);
/*
    sf_show_block(allocated_block);
    fprintf(stderr, "\n");

    sf_show_heap();
    fprintf(stderr, "\n");
*/
    void *payload = (void *)((char *)allocated_block + sizeof(sf_header));

    return payload;
}

/*
    Checks the validity of the ptr based on the assignment description

    The pointer is NULL.
    The pointer is not 32-byte aligned.
    The block size is less than the minimum block size of 32.
    The block size is not a multiple of 32
    The header of the block is before the start of the first block
    of the heap, or the footer of the block is after the end of the last
    block in the heap.
    The allocated bit in the header is 0.
    The prev_alloc field in the header is 0, indicating that the previous
    block is free, but the alloc field of the previous block header is not 0.
*/
int check_pointer(void *ptr, sf_block *block) {
    if ((uintptr_t)ptr & 31) return 1;
    if (get_block_size(block) < 32) return 1;
    if (get_block_size(block) % 32 != 0) return 1;
    if ((void *)block < sf_mem_start()) return 1;
    if ((void *)block + get_block_size(block) > sf_mem_end()) return 1;
    if (!get_curr_alloc_bit(block)) return 1;
    if (!get_prev_alloc_bit(block)) {
        sf_footer *prev_footer = (sf_footer *)((char *)block - sizeof(sf_footer));
        size_t prev_block_size = *prev_footer & ~0x1F;
        sf_block *prev_block = (sf_block *)((void *)prev_footer - prev_block_size + sizeof(sf_footer));
        if ((get_curr_alloc_bit(prev_block))) return 1; // evaluates true when alloc field is anything but 0
    }
    return 0; // the pointer was valid
}

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */
void sf_free(void *ptr) {
    sf_block *block_freed = (void *)((char *)ptr - sizeof(sf_header));

    if (check_pointer(ptr, block_freed)) {
        //fprintf(stderr, "ERROR: invalid pointer argument to free, sf_errno set\n");
        sf_errno = EINVAL;
        abort();
    }

    int prev_bit = 0;
    if (get_prev_alloc_bit(block_freed)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    // Write the free block information
    write_block_header(block_freed, get_block_size(block_freed), prev_bit, 0);

    sf_block *next_free_block = NULL;
    sf_block *prev_free_block = NULL;

    block_freed->body.links.next = next_free_block;
    block_freed->body.links.prev = prev_free_block;

    sf_block *next_block = get_block_end(block_freed);
    set_prev_alloc_bit(next_block, get_curr_alloc_bit(block_freed));

    block_freed = coalesce(block_freed);

    add_block_free_list_LIFO(block_freed);
    return;
}

/*
    When reallocating to a larger size, always follow these three steps:
        1) Call sf_malloc to obtain a larger block.

        2) Call memcpy to copy the data in the block given by the client to the block
        returned by sf_malloc.  Be sure to copy the entire payload area, but no more.

        3) Call sf_free on the block given by the client (inserting into a freelist
        and coalescing if required).

    Return the block given to you by sf_malloc to the client.

    If sf_malloc returns NULL, sf_realloc must also return NULL. Note that
    you do not need to set sf_errno in sf_realloc because sf_malloc should
    take care of this.
*/
void *sf_realloc_larger_size(void *ptr, size_t size, sf_block* client_block) {
    sf_block *larger_block = sf_malloc(size); // (step 1)

    memcpy(larger_block, ptr, get_block_size(client_block) - sizeof(sf_header)); // (step 2 - copies the payload)

    if (larger_block == NULL) return NULL; // (appended note)

    sf_free(ptr); // (step 3)

    return (void *)larger_block;
}

/*
    1) Splitting the returned block results in a splinter. In this case, do not
    split the block. Leave the splinter in the block, update the header field
    if necessary, and return the same block back to the caller.

        Block is not split.

    2) The block can be split without creating a splinter. In this case, split the
    block and update the block size fields in both headers.  Free the newly created
    remainder block by inserting it into the appropriate free list (after coalescing,
    if possible).  Return a pointer to the payload of the now-smaller block to the caller.

    return a pointer to the same block that was given to you.

    When reallocating to a smaller size, your allocator must use the block that was
    passed by the caller.  You must attempt to split the returned block.

    The two sub cases are already handeled by the way my malloc functions.
*/
void *sf_realloc_smaller_size(void *ptr, size_t size_req_aligned) {
    sf_block *client_block = (void *)((char *)ptr - sizeof(sf_header));

    size_t remaining_size = get_block_size(client_block) - size_req_aligned;
    if (remaining_size < MIN_BLOCK_SIZE) {
        return ptr; // splitting would cause a splinter update the header field with what?
    }

    int prev_bit = 0;
    if (get_prev_alloc_bit(client_block)) {
        // means previous bit is allocated set to 1
        prev_bit = 1;
    }

    // Write header for the allocated block
    sf_block *allocated_portion = write_block_header(client_block, size_req_aligned, prev_bit, 1);

    // Write the portion for the free block
    sf_block *new_free_block = write_block_header(((sf_block *)((void *)allocated_portion + get_block_size(allocated_portion))), remaining_size, prev_bit, 0);
    // sf_show_block(new_free_block);
    // fprintf(stderr, "\n");

    add_block_free_list_LIFO(new_free_block);

    return ptr;
}

/*
 * Resizes the memory pointed to by ptr to size bytes.
 *
 * @param ptr Address of the memory region to resize.
 * @param size The minimum size to resize the memory to.
 *
 * @return If successful, the pointer to a valid region of memory is
 * returned, else NULL is returned and sf_errno is set appropriately.
 *
 *   If sf_realloc is called with an invalid pointer sf_errno should be set to EINVAL.
 *   If there is no memory available sf_realloc should set sf_errno to ENOMEM.
 *
 * If sf_realloc is called with a valid pointer and a size of 0 it should free
 * the allocated block and return NULL without setting sf_errno.
*/
void *sf_realloc(void *ptr, size_t size) {
    sf_block *realloc_block = (void *)((char *)ptr - sizeof(sf_header));

    if (check_pointer(ptr, realloc_block)) {
        //fprintf(stderr, "ERROR: invalid pointer argument to realloc, sf_errno set\n");
        sf_errno = EINVAL;
        return NULL;
    }

    if (size == 0) {
        sf_free(ptr); // realloc size 0 then free
    }

    if (align_size(size) == get_block_size(realloc_block)) {
        return ptr; // nothing to be reallocated
    }

    if (size > get_block_size(realloc_block)) {
        return sf_realloc_larger_size(ptr, size, realloc_block);
    }

    if (size < get_block_size(realloc_block)) {
        return sf_realloc_smaller_size(ptr, align_size(size));
    }

    return NULL;
}

sf_block *free_portion(sf_block *free_part, size_t size, int prev_bit) {
    write_block_header(free_part, size, prev_bit, 0);

    sf_block *next_free_block = NULL;
    sf_block *prev_free_block = NULL;

    free_part->body.links.next = next_free_block;
    free_part->body.links.prev = prev_free_block;

    sf_block *next_block = get_block_end(free_part);
    set_prev_alloc_bit(next_block, get_curr_alloc_bit(free_part));

    free_part = coalesce(free_part);

    add_block_free_list_LIFO(free_part);

    return free_part;
}

sf_block *remove_front_free(sf_block *block_allocated, size_t offset, size_t adjusted_size) {
    int prev_bit = get_prev_alloc_bit(block_allocated);

    sf_block *free_start = free_portion(block_allocated, offset, prev_bit);

    sf_block *temp_block = write_block_header(get_block_end(free_start), align_size(adjusted_size) - offset, 0, 1);
    set_prev_alloc_bit(get_block_end(temp_block), 1);

    return temp_block;
}

void remove_end_free(sf_block *temp_block, size_t adjusted_size, size_t size, size_t offset) {
    int prev_bit = get_prev_alloc_bit(temp_block);
    int free_block_size = align_size(adjusted_size) - align_size(size) - offset + MIN_BLOCK_SIZE;

    sf_block *aligned_block = write_block_header(temp_block, align_size(size), prev_bit, 1);
    set_prev_alloc_bit(get_block_end(temp_block), 1);

    free_portion(get_block_end(aligned_block), free_block_size, 1); // writing the end free block
}

void malloc_ret_aligned_addr(sf_block *block_allocated, size_t adjusted_size, size_t size) {
        int prev_bit = get_prev_alloc_bit(block_allocated);
        int free_block_size = align_size(adjusted_size) - align_size(size);

        sf_block *aligned_block = write_block_header(block_allocated, align_size(size), prev_bit, 1);
        set_prev_alloc_bit(get_block_end(aligned_block), 1);

        free_portion(get_block_end(aligned_block), free_block_size, 1); // writing the end free block
}

void *increment_address(void *aligned_address) {
    return (void *)((char *)aligned_address + sizeof(sf_header));
}

size_t update_offset(size_t offset) {
    return offset += sizeof(sf_header);
}

void addr_aligned_free_end(void *block_payload, size_t adjusted_size, size_t size) {
    // JUST FREE THE END PORTION OFF (MALLOC ALREADY RETURNED AN ALIGNED ADDRESS FOR THE HEADER OF THE ALLOCATED BLOCK)
    // malloc returns a pointer to the payload of the allocated block, so move up pointer to actual block start (header)

    sf_block *block_allocated = (void *)((char *)block_payload - sizeof(sf_header));
    malloc_ret_aligned_addr(block_allocated, adjusted_size, size);
}

void *chng_addr_free_front_end(void *block_payload, size_t adjusted_size, size_t size, size_t align) {
    // malloc returns a pointer to the payload of the allocated block, so move up pointer to actual block start (header)
    sf_block *block_allocated = (void *)((char *)block_payload - sizeof(sf_header));

    size_t offset = MIN_BLOCK_SIZE;
    void *aligned_address = (void *)((char *)block_allocated + MIN_BLOCK_SIZE);

    while (((uintptr_t)aligned_address % align) != 0 ) {
        aligned_address = increment_address(aligned_address);
        offset = update_offset(offset);
    }
/*
    fprintf(stderr, "This is the aligned address: %p\n", aligned_address);
*/
    sf_block *temp_block = remove_front_free(block_allocated, offset, adjusted_size);

    remove_end_free(temp_block, adjusted_size, size, offset);

    return aligned_address;
}

/*
 * Allocates a block of memory with a specified alignment.
 *
 * @param align The alignment required of the returned pointer.
 * @param size The number of bytes requested to be allocated.
 *
 * @return If align is not a power of two or is less than the minimum block size,
 * then NULL is returned and sf_errno is set to EINVAL.
 * If size is 0, then NULL is returned without setting sf_errno.
 * Otherwise, if the allocation is successful a pointer to a valid region of memory
 * of the requested size and with the requested alignment is returned.
 * If the allocation is not successful, then NULL is returned and sf_errno is set
 * to ENOMEM.
 */
void *sf_memalign(size_t size, size_t align) {
    // this function ensures that the allocated memory block starts at an address that is a multiple of the alignment value provided by the user
    // the "align" argument will specify how the starting address of the allocated block should be aligned
    // to be successful the aligned block address must meet the condition: block_address % alignment = 0

    // Ensure alignment is a power of two and greater than or equal to the minimum block size
    if (align < MIN_BLOCK_SIZE|| (align & (align - 1)) != 0) {
        //fprintf(stderr, "ERROR: alignment not a power of 2 or >= minimum block size, sf_errno set\n");
        sf_errno = EINVAL;
        return NULL;
    }

    if (size == 0) return NULL;

    // in order to obtain memory with the requested alignment, memalign allocates a larger block than requested
    // it attempts to allocate a block size (at least) >= requested size + alignment size + minimum blocksize + size required for header and footer

    size_t adjusted_size = size + align + MIN_BLOCK_SIZE + sizeof(sf_footer); // the size space for the header is added in malloc when aligning the size to multiples 32
    // since can't control where the memory will be initially allocated, need to allocate slightly more memory than requested to allow for adjustment

    void *block_payload = sf_malloc(adjusted_size); // allocating the block
    if (block_payload == NULL) {
        //fprintf(stderr, "ERROR: in memalign returned from malloc, sf_errno set\n");
        sf_errno = ENOMEM;
        return NULL;
    }

    // after allocating the block, must find address that meets alignment requirements
    // adjust the address by adding the offset until find satisfying aligned address

    if (((uintptr_t)block_payload % align) == 0) {
/*
        fprintf(stderr, "This is the memory address returned by malloc: %p and this was the align value: %ld\n", block_payload, align);

        fprintf(stderr, "I don't know how to write the criterion tests so it also knows this is correct\n");
        sf_show_heap();
        fprintf(stderr, "\n");
*/

        addr_aligned_free_end(block_payload, adjusted_size, size);
/*
        fprintf(stderr, "Expected size of used block %ld\n", align_size(size));
        sf_show_heap();
*/
        return block_payload; // this address is already aligned
    }

    void *aligned_address = chng_addr_free_front_end(block_payload, adjusted_size, size, align);
/*
    fprintf(stderr, "Expected size of used block %ld\n", align_size(size));
    sf_show_heap();
*/
    return aligned_address; // return the correctly aligned address to the user (NULL if no memory, will be returned by MALLOC above)
}