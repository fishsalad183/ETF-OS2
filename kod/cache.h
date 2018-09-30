#pragma once


#include <mutex>
#include <iostream>


#define NAME_LENGTH (20)

#define ERROR_NO_MEMORY (1)
#define ERROR_FREEING_OBJECT (2)
#define ERROR_DELETING_SLAB (3)
#define SHRINKING_AVOIDED (4)


class Allocator;
class Slab;



class Cache {
private:
	char name[NAME_LENGTH];
	size_t slotSize;
	int optimalNumOfSlotsPerSlab;
	void (*constructor)(void *);
	void (*destructor)(void *);

	static Cache* headCache;
	Cache* nextCache;

	Slab* slabsFullHead;
	Slab* slabsPartialHead;
	Slab* slabsFreeHead;
	int numOfSlabs;

	bool slabAllocatedSinceLastShrink;
	bool shrinkDone;

	int alignments;
	int current_alignment;

	int error_code;

	int destroySlab(Slab* s);

	Cache(const char* name, size_t size, void (*ctor)(void *), void (*dtor)(void *));

	std::recursive_mutex m;
public:
	static Cache* createCache(const char* name, size_t size, void(*ctor)(void *), void(*dtor)(void *));
	static Cache* createCacheForCaches();

	inline static Cache* getHeadCache() {
		return headCache;
	}

	inline Cache* getNextCache() const {
		return nextCache;
	}

	int shrink();
	void* alloc();
	bool free(void* objp);
	void destroy();
	void info();

	inline int getErrorCode() const {
		return error_code;
	}
};



struct kmem_cache_s {
private:
	friend class Allocator;
	Cache* c;

	inline void destroy() {
		if (c) c->destroy();
		else exit(3);
	}
public:
	kmem_cache_s(Cache* cache);
	inline int shrink() const {
		if (c) return c->shrink();
		else exit(3);
	}
	inline void* alloc() const {
		if (c) return c->alloc();
		else exit(3);
	}
	inline bool free(void* objp) const {
		if (c) return c->free(objp);
		else exit(3);
	}
	inline void info() const {
		if (c) c->info();
		else std::cout << "Handle does not point to any cache!" << std::endl;
	}
	int error() const;
};