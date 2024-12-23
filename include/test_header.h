#ifndef test_header_h
#define test_header_h

#include "sfmm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MIN_BLOCK_SIZE 32
#define PROLOGUE_SIZE 32
#define CURR_BLOCK_ALLOC 0x10
#define PREV_BLOCK_ALLOC 0x8
#define EPILOGUE_SIZE 8
#define PROLOGUE_SIZE 32

size_t align_size(size_t size);
size_t get_block_size(sf_block *block_ptr);
int set_block_size(sf_block *block, size_t size);
int set_prev_alloc_bit(sf_block *block, int flag);
int get_prev_alloc_bit(sf_block *block);
int set_curr_alloc_bit(sf_block *block, int flag);
int get_curr_alloc_bit(sf_block *block);
sf_block *get_block_end(sf_block *block);
sf_footer *write_footer_only_free_blocks(sf_block *block);
sf_block *write_block_header(sf_block *block, size_t size, int prev_alloc, int alloc);
sf_block *unlink_block_from_free_list_return_malloc_request(sf_block *block);
void initialize_free_lists(int index);
int get_free_list_index(size_t size);
sf_block *get_free_list_head_to_search_for_block(size_t size);
sf_block *find_and_remove_exact_match_free_list_block(sf_block *free_list_head_pntr, size_t size);
sf_block *coalesce_next(sf_block *block);
sf_block *coalesce_prev(sf_block *block);
sf_block *coalesce(sf_block *block);
sf_block *coalesce_if_possible(sf_block *block);
void insert_block_to_free_list(sf_block *block);
sf_block *add_block_free_list_LIFO(sf_block *block);
sf_block *split_free_block(sf_block *free_block_part_remaining_put_back, sf_block *allocated_block_to_return, size_t requested_size_plus_minBlock);
void remove_from_free_list(sf_block *block);
sf_block *allocate_block_with_split_from_free(sf_block *split_part_satisfy_malloc_request, sf_block *free_block_to_split, size_t size);
sf_block *allocate_block_waste_space(sf_block *free_block_to_return, size_t size);
sf_block *find_and_allocate_block_no_split_waste_space(int free_list_index_matched, size_t size);
sf_block *find_and_allocate_block_split_no_splinter(int free_list_index_matched, size_t size);
void *padding(void *startAddr);
sf_block *grow_heap();
sf_block *get_free_list_block(size_t size);
int init_heap();
int check_initialized_heap();
sf_block *process_payload(sf_block *free_list_block_ret);
void *sf_malloc(size_t size);
int check_pointer(void *ptr, sf_block *block);
void sf_free(void *ptr);
void *sf_realloc_larger_size(void *ptr, size_t size, sf_block* client_block);
void *sf_realloc_smaller_size(void *ptr, size_t size_req_aligned);
void *sf_realloc(void *ptr, size_t size);
void *sf_memalign(size_t size, size_t align);


#endif