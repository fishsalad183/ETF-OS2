#include "allocator.h"
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>


void* Allocator::space = nullptr;
int Allocator::block_num = 0;
bool Allocator::is_initialized = false;
int Allocator::buddy[] = { 0 };
Cache* Allocator::cache_for_handles = nullptr;
Cache* Allocator::cache_for_caches = nullptr;
Cache* Allocator::sizes[] = { nullptr };
std::recursive_mutex Allocator::m;


void Allocator::init(void *space, int block_num) {
	if (Allocator::is_initialized) {
		std::cout << "Allocator has already been initialized!" << std::endl;
		return;
	}
	if (block_num < 0 || block_num >= (1 << N)) {
		std::cout << "NUMBER OF BLOCKS (" + std::to_string(block_num) + ") EXCEEDING MAXIMUM (2^" + std::to_string(N) + " - 1)" << std::endl;
		exit(4);
	}

	Allocator::space = space;
	Allocator::block_num = block_num;

	int i = N - 1;
	int mask = 1 << (N - 1);
	int blocks_offset = 0;
	while (i >= 0) {
		int blocks_i = block_num & mask;
		if (blocks_i) {
			buddy[i] = blocks_offset;
			*((int*)block(blocks_offset)) = -1;
			blocks_offset += blocks_i;
		}
		else buddy[i] = -1;
		--i;
		mask >>= 1;
	}

	cache_for_caches = Cache::createCacheForCaches();
	if (!cache_for_caches) exit(1);	// Fatal error.
	cache_for_handles = Cache::createCache("CACHE FOR HANDLES", sizeof(kmem_cache_t), nullptr, nullptr);
	if (!cache_for_handles) exit(2);	// Fatal error.

	// SIZE CACHES ARE CREATED IMPLICITLY WHEN THERE IS NEED TO ALLOCATE A CERTAIN SIZE BUFFER FOR THE FIRST TIME

	Allocator::is_initialized = true;
}


void* Allocator::block(int n) {
	if (n < 0 || n >= block_num) return nullptr;	// will return nullptr even if allocator is uninitialized because then block_num is 0
	return (void*)((char*)space + n*BLOCK_SIZE);
}


int Allocator::find_buddy(int n, int i) {
	if (n < 0 || n >= block_num || i < 0 || i >= N) return -1;	// Error: n or i out of range.
	int size_in_blocks = 1;
	for (int j = 0; j < i; j++) size_in_blocks *= 2;
	if (n % size_in_blocks != 0) return -1;	// Error: misaligned n.
	int chunk_position = n / size_in_blocks;
	// The code below checks if the block has no buddy (due to block_num not being a power of 2).
	// A different "error" value is returned (-2, not -1) in case there is no buddy.
	if (chunk_position % 2 == 0) return (n + size_in_blocks * 2 > block_num) ? -2 : n + size_in_blocks;
	else return n + size_in_blocks > block_num ? -2 : n - size_in_blocks;
}


void* Allocator::buddy_alloc(int i) {
	m.lock();
	if (i < 0 || i >= N) {	// error
		m.unlock();
		return nullptr;
	}
	// First, try to find the segment of exact size of 2^i blocks:
	if (buddy[i] > -1) {
		// Found! Remove it from the list buddy[i] and return it:
		void* ret = block(buddy[i]);
		buddy[i] = *((int*)ret);
		m.unlock();
		return ret;
	}
	// Else, find the first next bigger segment:
	for (int j = i + 1; j < N; j++) {
		int seg1 = buddy[j];
		if (seg1 > -1) {
			// Found. Divide it into two halves:
			int seg2 = seg1 + (1 << (j-1));
			int* pSeg1 = (int*)block(seg1);
			int* pSeg2 = (int*)block(seg2);
			// Remove it from buddy[j]:
			buddy[j] = *pSeg1;
			// Add two segments to buddy[j-1]:
			*pSeg2 = buddy[j - 1];
			*pSeg1 = seg2;
			buddy[j - 1] = seg1;
			// Now, try again from the beginning (recursion).
			// You will find it eventually!
			void* ret = buddy_alloc(i);
			m.unlock();
			return ret;
		}
	}
	m.unlock();
	// Not found, no memory:
	return nullptr;
}


void* Allocator::buddy_alloc_blocks_required(int blocks) {
	if (blocks <= 0) return nullptr;	// Error. However, if number of blocks is too large,
										// the error will be addressed in buddy_alloc(i).
	int i = 0;
	for (int n = 1; n < blocks; n *= 2) i++;
	return buddy_alloc(i);
}


void* Allocator::buddy_alloc_space_required(int bytes) {
	if (bytes <= 0) return nullptr;	// error
	int blocks = bytes / BLOCK_SIZE;
	if (bytes % BLOCK_SIZE > 0) ++blocks;
	return buddy_alloc_blocks_required(blocks);
}


int Allocator::bytes_required_to_blocks_allocated(int bytes) {
	if (bytes <= 0) return -1;	// error
	int blocks = bytes / BLOCK_SIZE;
	if (bytes % BLOCK_SIZE > 0) ++blocks;
	int n = 1;
	while (n < blocks) n *= 2;
	return n;
}


int Allocator::buddy_free(int n, int i) {
	m.lock();
	if (i < 0 || i >= N) {	// Error: i out of range.
		m.unlock();
		return -1;
	}
	int* pn = (int*)block(n);
	if (pn == nullptr) {	// Error: illegal block n.
		m.unlock();
		return -1;
	}
	int nb = find_buddy(n, i);	// Find the buddy of n.
	if (nb == -1) {	// Error: mismatching n and i.
		m.unlock();
		return -1;
	}

	// First, try to find the buddy of block n in the list buddy[i]:
	int prev = -1, cur = buddy[i];
	while (cur != -1 && cur != nb) {
		prev = cur;
		int* pc = (int*)block(cur);
		if (pc == nullptr) {	// Unexpected error: corrupted structure.
			m.unlock();
			return -1;
		}
		cur = *pc;
	}

	if (cur == -1) {	// Not found; just add block n to the list buddy[i].
		*pn = buddy[i];
		buddy[i] = n;
		m.unlock();
		return 0;
	}

	// Found the buddy. Remove it from buddy[i]:
	int* pc = (int*)block(cur);
	if (pc == 0) {	// Unexpected error: corrupted structure.
		m.unlock();
		return -1;
	}
	if (prev != -1) {
		int* pp = (int*)block(prev);
		if (pp == nullptr) {	// Unexpected error: corrupted structure.
			m.unlock();
			return -1;
		}
		*pp = *pc;
	}
	else
		buddy[i] = *pc;
	*pc = -1;

	// Then join n and its buddy nb to one block nm and add it to buddy[i+1]:
	int nm = n < nb ? n : nb;	// nm = min(n, nb)
	if (i < N - 1) {	// Recursion.
		int ret = buddy_free(nm, i + 1);
		m.unlock();
		return ret;
	}
	else {
		m.unlock();
		return -1;	// Unexpected error: should never occur.
	}
}


int Allocator::deallocate(void* space_to_free, int num_of_blocks) {
	int first_block = ((char*)space_to_free - (char*)space) / BLOCK_SIZE;
	int i = 0;
	for (int blocks = 1; blocks < num_of_blocks; blocks *= 2, i++);
	return buddy_free(first_block, i);
}


void* Allocator::allocateMemoryForCacheCreation() {
	return cache_for_caches != nullptr ? cache_for_caches->alloc() : nullptr;
}


kmem_cache_t* Allocator::cache_create(const char *name, size_t size, void(*ctor)(void *), void(*dtor)(void *)) {
	Cache* c = Cache::createCache(name, size, ctor, dtor);
	kmem_cache_t* ret = (kmem_cache_t*)cache_for_handles->alloc();
	ret->c = c;
	return ret;
}


void* Allocator::malloc(size_t size) {
	size_t lower_limit = 0;
	size_t upper_limit = MIN_SIZE_POWER_OF_2_BYTES;
	for (int i = 0; i < SIZES; i++) {
		if (size > lower_limit && size <= upper_limit) {
			if (!sizes[i]) {
				// Create size-N cache if one does not exist.
				std::string s = "size-";
				s += std::to_string(upper_limit);
				sizes[i] = Cache::createCache(s.c_str(), upper_limit, nullptr, nullptr);
				if (!sizes[i]) return nullptr;	// error
			}
			return sizes[i]->alloc();
		}
		lower_limit = upper_limit;
		upper_limit *= 2;
	}
	return nullptr;
}


void Allocator::free(const void* objp) {
	for (int i = 0; i < SIZES; i++) {
		if (sizes[i] != nullptr && sizes[i]->free((void*)objp) == true) {
			sizes[i]->shrink();	// It is neccessary to shrink sizes here because it cannot be done from outside.
								// Also - FORCE SHRINK? (Create special shrink method that cannot be avoided - see Cache::shrink()).
			return;
		}
	}
}


void Allocator::cache_destroy(kmem_cache_t* cachep) {
	cachep->destroy();
	cache_for_caches->free(cachep->c);
	cache_for_handles->free(cachep);
	cachep->c = nullptr;
}


void Allocator::sizes_info(int index) {
	if (index < 0 || index >= SIZES) return;
	if (sizes[index]) sizes[index]->info();
}


int Allocator::sizes_error(int index) {
	if (index < 0 || index >= SIZES) return -1;
	if (sizes[index]) return sizes[index]->getErrorCode();
	else return -1;
}


/*
int Allocator::cache_shrink(Cache* cachep) {
	m.lock();
	for (Cache* c = Cache::getHeadCache(); c != nullptr; c = c->getNextCache()) {
		if (c == cachep) {
			m.unlock();
			return c->shrink();
		}
	}
	m.unlock();
	return -1;	// Error: cache not found.
}


void* Allocator::cache_alloc(Cache* cachep) {
	m.lock();
	for (Cache* c = Cache::getHeadCache(); c != nullptr; c = c->getNextCache()) {
		if (c == cachep) {
			m.unlock();
			return c->alloc();
		}
	}
	m.unlock();
	return nullptr;	// Error: cache not found.
}


void Allocator::cache_free(Cache* cachep, void* objp) {
	m.lock();
	for (Cache* c = Cache::getHeadCache(); c != nullptr; c = c->getNextCache()) {
		if (c == cachep) {
			m.unlock();
			c->free(objp);
			return;
		}
	}
}


void Allocator::cache_destroy(Cache* cachep) {
	m.lock();
	for (Cache* c = Cache::getHeadCache(); c != nullptr; c = c->getNextCache()) {
		if (c == cachep) {
			c->destroy();
			break;
		}
	}
	m.unlock();
	cache_for_caches->free(cachep);
//	cache_for_handles->free(	//????
}


void Allocator::cache_info(Cache* cachep) {
	m.lock();
	for (Cache* c = Cache::getHeadCache(); c != nullptr; c = c->getNextCache()) {
		if (c == cachep) {
			m.unlock();
			c->info();
			return;
		}
	}
	m.unlock();
}


int Allocator::cache_error(Cache* cachep) {
	m.lock();
	for (Cache* c = Cache::getHeadCache(); c != nullptr; c = c->getNextCache()) {
		if (c == cachep) {
			m.unlock();
			return c->error();
		}
	}
	m.unlock();
	return -1;	// value?
}
*/