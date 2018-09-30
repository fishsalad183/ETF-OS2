#include "cache.h"
#include "allocator.h"
#include "slab.h"
#include "slab class.h"
#include <new>
#include <string>
#include <iomanip>



Cache* Cache::headCache = nullptr;


Cache* Cache::createCacheForCaches() {
	void* loc = Allocator::buddy_alloc_space_required(sizeof(Cache));
	if (loc == nullptr) return nullptr;	// error
	Cache* c = new (loc) Cache("CACHE FOR CACHES", sizeof(Cache), nullptr, nullptr);	// Placement new!
	return c;
}


Cache* Cache::createCache(const char* name, size_t size, void(*ctor)(void *), void(*dtor)(void *)) {
	void* loc = Allocator::allocateMemoryForCacheCreation();
	if (loc == nullptr) return nullptr;	// error
	Cache* c = new (loc) Cache(name, size, ctor, dtor);	// Placement new!
	return c;
}


Cache::Cache(const char* name, size_t size, void(*ctor)(void *), void(*dtor)(void *)) {
	snprintf(this->name, NAME_LENGTH, "%s", name);
	slotSize = size;
	optimalNumOfSlotsPerSlab = Slab::optimalNumOfSlotsPerSlab(size);
	constructor = ctor;
	destructor = dtor;

	nextCache = headCache;
	headCache = this;

	slabsFullHead = nullptr;
	slabsPartialHead = nullptr;
	slabsFreeHead = nullptr;
	numOfSlabs = 0;

	slabAllocatedSinceLastShrink = false;
	shrinkDone = false;

	alignments = Slab::unusedSpaceWithOptimalSlots(size) / CACHE_L1_LINE_SIZE;
	current_alignment = 0;

	error_code = 0;
}


void* Cache::alloc() {
	m.lock();

	void* ret;

	if (slabsPartialHead != nullptr) {
		Slab* s = slabsPartialHead;
		ret = s->alloc(constructor);
		if (s->isFull()) {
			slabsPartialHead = slabsPartialHead->getNext();
			s->setNext(slabsFullHead);
			slabsFullHead = s;
		}
		m.unlock();
		return ret;
	}

	if (slabsFreeHead != nullptr) {
		Slab* s = slabsFreeHead;
		ret = s->alloc(constructor);
		if (s->isFull()) {	// in case there is only one object per slab
			slabsFreeHead = slabsFreeHead->getNext();
			s->setNext(slabsFullHead);
			slabsFullHead = s;
		}
		else {
			slabsFreeHead = slabsFreeHead->getNext();
			s->setNext(slabsPartialHead);
			slabsPartialHead = s;
		}
		m.unlock();
		return ret;
	}

	Slab* s = Slab::createSlab(optimalNumOfSlotsPerSlab, slotSize, constructor, current_alignment);
	if (!s) {
		error_code = ERROR_NO_MEMORY;
		m.unlock();
		return nullptr;
	}
	/*
	// IF VALUES EXCEPT OPTIMAL ARE ALLOWED, SLABS MUST FIX OFFSET IN CASES OF INADEQUATE VALUES
	if (!s) {	// error, attempt to allocate less memory
		s = Slab::createSlab(Slab::minimalNumOfSlotsPerSlab(slotSize), slotSize, constructor, current_alignment);
		if (!s) {	// error, no memory
			error_code = ERROR_NO_MEMORY;
			m.unlock();
			return nullptr;
		}
	}*/

	ret = s->alloc(constructor);
	numOfSlabs++;
	if (alignments != 0) current_alignment = (current_alignment + 1) % alignments;
	if (s->isFull()) {	// in case there is only one object per slab
		s->setNext(slabsFullHead);
		slabsFullHead = s;
	}
	else {
		s->setNext(slabsPartialHead);
		slabsPartialHead = s;
	}
	if (shrinkDone == true) {
		slabAllocatedSinceLastShrink = true;
		shrinkDone = false;
	}
	else if (slabAllocatedSinceLastShrink == true) {
		shrinkDone = false;
		slabAllocatedSinceLastShrink = false;
	}
	m.unlock();
	return ret;
}


bool Cache::free(void* objp) {
	m.lock();

	for (Slab *s = slabsFullHead, *prev = nullptr; s != nullptr; prev = s, s = s->getNext()) {
		if (s->free(objp) == true) {
			if (prev) prev->setNext(s->getNext());
			else slabsFullHead = slabsFullHead->getNext();
			if (s->isEmpty()) {
				s->setNext(slabsFreeHead);
				slabsFreeHead = s;
			}
			else {
				s->setNext(slabsPartialHead);
				slabsPartialHead = s;
			}
			m.unlock();
			return true;
		}
	}
	for (Slab* s = slabsPartialHead, *prev = nullptr; s != nullptr; prev = s, s = s->getNext()) {
		if (s->free(objp) == true) {
			if (s->isEmpty()) {
				if (prev) prev->setNext(s->getNext());
				else slabsPartialHead = slabsPartialHead->getNext();
				s->setNext(slabsFreeHead);
				slabsFreeHead = s;
			}
			m.unlock();
			return true;
		}
	}

	error_code = ERROR_FREEING_OBJECT;

	m.unlock();

	return false;
}


int Cache::destroySlab(Slab* s) {
	m.lock();
	s->destroyObjects(destructor);
	int ret = Allocator::deallocate(s->getSpace(), s->getNumOfBlocks());
	if (ret != 0) error_code = ERROR_DELETING_SLAB;
	m.unlock();
	return ret;
}


void Cache::destroy() {
//	if (m.try_lock() == false) return;	// Means that another thread is currently destroying this Cache.
	m.lock();

	Slab* cur = slabsFreeHead;
	while (cur != nullptr) {
		slabsFreeHead = slabsFreeHead->getNext();
		destroySlab(cur);
		cur = slabsFreeHead;
	}
	cur = slabsPartialHead;
	while (cur != nullptr) {
		slabsPartialHead = slabsPartialHead->getNext();
		destroySlab(cur);
		cur = slabsPartialHead;
	}
	cur = slabsFullHead;
	while (cur != nullptr) {
		slabsFullHead = slabsFullHead->getNext();
		destroySlab(cur);
		cur = slabsFullHead;
	}

	for (Cache *cur = headCache, *prev = nullptr; cur != nullptr; cur = cur->nextCache) {
		if (cur == this) {
			if (prev) prev->nextCache = cur->nextCache;
			else headCache = cur->nextCache;
		}
		prev = cur;
	}

	m.unlock();
}


int Cache::shrink() {
	m.lock();

	if (slabAllocatedSinceLastShrink) { // If slab allocation has occured since last shrinking, then return. 0 or some other value?
		error_code = SHRINKING_AVOIDED;
		m.unlock();
		return 0; 
	}
	Slab* cur = slabsFreeHead;
	if (!cur) {
		m.unlock();
		return 0;
	}
	int blocks_freed = 0;
	while (cur != nullptr) {
		slabsFreeHead = cur->getNext();
		int blocks_cur = cur->getNumOfBlocks();
		if (destroySlab(cur) == 0) blocks_freed += blocks_cur;	// Should always happen.
		numOfSlabs--;
		cur = slabsFreeHead;
	}
	shrinkDone = true;

	m.unlock();
	return blocks_freed;
}


void Cache::info() {
	m.lock();
	std::string s = "";
	s += name; s += '\n';
	s += std::to_string(slotSize); s += " B/obj\n";
	s += std::to_string(numOfSlabs * Slab::blocksOccupied(slotSize)); s += " blocks\n";
	s += std::to_string(numOfSlabs); s += " slabs\n";
	s += std::to_string(optimalNumOfSlotsPerSlab); s += " obj/slab\n";
	int slots_occupied = 0;
	int total_slots = 0;
	for (Slab* s = slabsFullHead; s != nullptr; s = s->getNext()) {
		slots_occupied += s->getSlotsOccupied();
		total_slots += s->getNumOfSlots();
	}
	for (Slab* s = slabsPartialHead; s != nullptr; s = s->getNext()) {
		slots_occupied += s->getSlotsOccupied();
		total_slots += s->getNumOfSlots();
	}
	for (Slab* s = slabsFreeHead; s != nullptr; s = s->getNext())
		total_slots += s->getNumOfSlots();
	if (total_slots != 0) {
		s += std::to_string((float)slots_occupied / (numOfSlabs * optimalNumOfSlotsPerSlab) * 100);
		s += "% full\n";
	}
	else
		s += "cache has no slots\n";
	std::cout << s << std::endl;
	m.unlock();
}






kmem_cache_s::kmem_cache_s(Cache* cache) : c(cache) {}


int kmem_cache_s::error() const {
	int ec = c->getErrorCode();
	switch (ec) {
	case 0: std::cout << "NO ERROR." << std::endl; break;
	case ERROR_NO_MEMORY: std::cout << "NO MEMORY AVAILABLE!" << std::endl; break;
	case ERROR_FREEING_OBJECT: std::cout << "ERROR FREEING OBJECT!" << std::endl; break;
	case ERROR_DELETING_SLAB: std::cout << "ERROR DELETING SLAB!" << std::endl; break;
	case SHRINKING_AVOIDED: std::cout << "SHRINKING AVOIDED!" << std::endl; break;
	default: std::cout << "UNDEFINED ERROR!" << std::endl; break;
	}
	return ec;
}