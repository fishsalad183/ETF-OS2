#include "slab class.h"
#include "allocator.h"
#include "slab.h"
#include <new>


Slab* Slab::createSlab(int numOfSlots, size_t slotSize, void (*constructor)(void *), int offset) {
	size_t space_req = numOfSlots * (slotSize + sizeof(bufctl)) + sizeof(Slab);
	void* space = Allocator::buddy_alloc_space_required(space_req);
	if (space == nullptr) return nullptr;	// error
	Slab* s = new (space) Slab(numOfSlots, slotSize, space, constructor, offset);	// Placement new!
	return s;
}


int Slab::optimalNumOfSlotsPerSlab(size_t slotSize) {
	int optimal_num_of_slots = 0;
	float max_ratio = 0;
	for (int i = 0, blocks = 1; i < N; i++) {
		int bytes_available = blocks * BLOCK_SIZE;
		int slots = (bytes_available - sizeof(Slab)) / (slotSize + sizeof(bufctl));
		int bytes_remaining = bytes_available - slots * (slotSize + sizeof(bufctl)) - sizeof(Slab);
		float ratio = (float)bytes_available / bytes_remaining;
		if (ratio >= 8.) return slots;	// if 1/8 or less of available space is wasted, it is immediately accepted
		if (ratio > max_ratio) {
			max_ratio = ratio;
			optimal_num_of_slots = slots;
		}
		if (i >= MAX_N_OPTIMAL && slots > 0) return optimal_num_of_slots;	// prevents too large numbers of blocks from being taken
		blocks *= 2;
	}
	return optimal_num_of_slots;
}


int Slab::minimalNumOfSlotsPerSlab(size_t slotSize) {
	for (int i = 0, blocks = 1; i < N; i++) {
		int slots = (blocks * BLOCK_SIZE - sizeof(Slab)) / (slotSize + sizeof(bufctl));
		if (slots > 0) return slots;
		blocks *= 2;
	}
	return -1;	// error
}


int Slab::unusedSpaceWithOptimalSlots(size_t slotSize) {
	int bytes_required = optimalNumOfSlotsPerSlab(slotSize) * (slotSize + sizeof(bufctl)) + sizeof(Slab);
	return Allocator::bytes_required_to_blocks_allocated(bytes_required) * BLOCK_SIZE - bytes_required;
}


int Slab::blocksOccupied(size_t slotSize) {
	int bytes_required = optimalNumOfSlotsPerSlab(slotSize) * (slotSize + sizeof(bufctl)) + sizeof(Slab);
	return Allocator::bytes_required_to_blocks_allocated(bytes_required);
}


Slab::Slab(int _numOfSlots, size_t _slotSize, void* _space, void (*constructor)(void *), int offset) {
	this->numOfSlots = _numOfSlots;
	this->slotSize = _slotSize;
	this->slotsOccupied = 0;
	this->space = _space;
	this->blocks = Allocator::bytes_required_to_blocks_allocated(_numOfSlots * (_slotSize + sizeof(bufctl)) + sizeof(Slab));
	this->nextSlab = nullptr;
	
	bufctl* cur_bufctl = (bufctl*)((char*)space + sizeof(Slab));	// Slab object is stored at the beginning of its allocated memory.
	/*
	if ((char*)space + blocks * BLOCK_SIZE < (char*)cur_bufctl + numOfSlots * (sizeof(bufctl) + slotSize) + offset * CACHE_L1_LINE_SIZE / sizeof(char))
		offset = 0;	// RESET OFFSET IN CASE OF INADEQUATE VALUE!
	*/
	this->object_space = (char*)cur_bufctl + numOfSlots * sizeof(bufctl) + offset * CACHE_L1_LINE_SIZE / sizeof(char);
	this->freeSlot = cur_bufctl;

	for (int i = 0; i < numOfSlots; i++) {
		bufctl b;
		b.next = (i == numOfSlots - 1) ? nullptr : cur_bufctl + 1;
		b.initialized = true;
		*cur_bufctl = b;
		cur_bufctl++;
		
		if (constructor) (*constructor)(getObject(i));
	}
}


bufctl* Slab::getBufctl(int index) {
	if (index < 0 || index > numOfSlots) return nullptr;
	bufctl* b = (bufctl*)((char*)space + sizeof(Slab));
	return b + index;
}


int Slab::getIndex(bufctl* b) {
	if (b == nullptr) return -1;	// error
	bufctl* first_bufctl = (bufctl*)((char*)space + sizeof(Slab));
	return b - first_bufctl;
}


void* Slab::getObject(int index) {
	return (char*)object_space + index * slotSize;
}


void* Slab::alloc(void (*constructor)(void *)) {
	if (freeSlot == nullptr) return nullptr;	// Error: no free slots.
	int i = getIndex(freeSlot);
	void* objp = getObject(i);
	if (freeSlot->initialized == false) {	// Just in case?
		if (constructor) (*constructor)(objp);
		freeSlot->initialized = true;
	}
	// No need to change freeSlot->next.
	freeSlot = freeSlot->next;	
	slotsOccupied++;
	return objp;
}


bool Slab::objectBelongsToSlab(void* objp) {
	if (((char*)objp >= object_space) && ((char*)objp < (char*)object_space + numOfSlots * slotSize)) return true;
	else return false;
}


bool Slab::free(void* objp) {
	if (!objectBelongsToSlab(objp)) return false;
	int index = ((char*)objp - (char*)object_space) / slotSize;
	bufctl* b = getBufctl(index);
	if (b == nullptr || b->initialized == false) return false;	// This means that no object has ever been allocated nor initialized in this slot.
	// OBJECTS ARE NOT DESTROYED IN ORDER TO AVOID CONSTRUCTION IF SAME SLOT IS ALLOCATED NEXT TIME.
	b->next = freeSlot;
	freeSlot = b;
	slotsOccupied--;
	return true;
}


void Slab::destroyObjects(void(*destructor)(void *)) {
	if (destructor)
		for (int i = 0; i < numOfSlots; i++)
			if (getBufctl(i)->initialized == true) (*destructor)(getObject(i));
}