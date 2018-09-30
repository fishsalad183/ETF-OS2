#include "slab.h"
#include "allocator.h"
#include "cache.h"



void kmem_init(void *space, int block_num) {
	Allocator::init(space, block_num);
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void(*ctor)(void *), void(*dtor)(void *)) {
	return Allocator::cache_create(name, size, ctor, dtor);
}

int kmem_cache_shrink(kmem_cache_t *cachep) {
	return cachep->shrink();
}

void *kmem_cache_alloc(kmem_cache_t *cachep) {
	return cachep->alloc();
}

void kmem_cache_free(kmem_cache_t * cachep, void * objp) {
	cachep->free(objp);
}

void *kmalloc(size_t size) {
	return Allocator::malloc(size);
}

void kfree(const void *objp) {
	Allocator::free(objp);
}

void kmem_cache_destroy(kmem_cache_t *cachep) {
	Allocator::cache_destroy(cachep);
}

void kmem_cache_info(kmem_cache_t *cachep) {
	cachep->info();
}

void kmem_sizes_info(int i) {
	Allocator::sizes_info(i);
}

int kmem_cache_error(kmem_cache_t *cachep) {
	return cachep->error();
}







