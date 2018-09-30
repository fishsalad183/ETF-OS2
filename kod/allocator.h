#pragma once


#include <mutex>
#include "slab.h"
#include "cache.h"


class Cache;


#define N (10)	// 2^N - 1 is the maximum number of blocks for the allocator
				// 2^(N-1) is the maximum number of blocks one chunk of memory can take
#define SIZES (13)
#define MIN_SIZE_POWER_OF_2_BYTES (32)


class Allocator {
private:
	static void* space;
	static int block_num;

	static bool is_initialized;

	static int buddy[N];

	static Cache* cache_for_handles;
	static Cache* cache_for_caches;
	static Cache* sizes[SIZES];

	Allocator() {}	// makes the class practically static

	static std::recursive_mutex m;
public:
	static void init(void *space, int block_num);

	static void* block(int n);
	static int find_buddy(int n, int i);	// returns the position of the first block of 
											// the buddy of the memory chunk that begins 
											// with block number n and is 2^i blocks large;
											// returns -1 if n is not an appropriate position for the first block
	static void* buddy_alloc(int i);	// returns 2^i continual blocks
	static void* buddy_alloc_blocks_required(int blocks);	// accepts total number of blocks as argument
	static void* buddy_alloc_space_required(int bytes);
	static int bytes_required_to_blocks_allocated(int bytes);
	static int buddy_free(int n, int i);
	static int deallocate(void* space_to_free, int num_of_blocks);

	static void* allocateMemoryForCacheCreation();

	static kmem_cache_t* cache_create(const char *name, size_t size, void(*ctor)(void *), void(*dtor)(void *));
/*	static int cache_shrink(Cache* cachep);
	static void* cache_alloc(Cache* cachep);
	static void cache_free(Cache* cachep, void* objp);*/
	static void* malloc(size_t size);
	static void free(const void* objp);
	static void cache_destroy(kmem_cache_t* cachep);
/*	static void cache_destroy(Cache* cachep);
	static void cache_info(Cache* cachep);
	static int cache_error(Cache* cachep);*/

	static void sizes_info(int index);	// Returns info for size-N.
	static int sizes_error(int index);	// Returns size-N error code.
};