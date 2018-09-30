#pragma once

#include "allocator.h"
#include "slab.h"

#define MAX_N_OPTIMAL (6)


struct bufctl {
	bufctl* next;
 	bool initialized;
	// Add const int index and remove Slab::getIndex(bufctl*)?
};


class Slab {
private:
	void* space;
	void* object_space;
	int numOfSlots;
	int slotsOccupied;
	size_t slotSize;
	int blocks;

	bufctl* freeSlot;

	Slab* nextSlab;

	Slab(int _numOfSlots, size_t _slotSize, void* _space, void(*constructor)(void *), int offset);	// objects are created from outside with static createSlab(...) method

	bufctl* getBufctl(int index);
	int getIndex(bufctl* b);

	void* getObject(int index);
public:
	static Slab* createSlab(int numOfSlots, size_t slotSize, void (*constructor)(void *), int offset);

	inline void* getSpace() const {
		return space;
	}

	inline size_t getTotalSize() const {
		return blocks * BLOCK_SIZE;
	}

	inline int getNumOfBlocks() const {
		return blocks;
	}

	inline int getNumOfSlots() const {
		return numOfSlots;
	}

	inline int getSlotsOccupied() const {
		return slotsOccupied;
	}

	inline size_t getSlotSize() const {
		return slotSize;
	}

	inline Slab* getNext() const {
		return nextSlab;
	}

	inline void setNext(Slab* s) {
		nextSlab = s;
	}

	inline bool isFull() const {
		return numOfSlots == slotsOccupied;
	}

	inline bool isEmpty() const {
		return slotsOccupied == 0;
	}

	void* alloc(void (*constructor)(void *));

	bool objectBelongsToSlab(void* objp);

	bool free(void* objp);

	void destroyObjects(void (*destructor)(void *));

	static int optimalNumOfSlotsPerSlab(size_t slotSize);
	static int minimalNumOfSlotsPerSlab(size_t slotSize);
	static int unusedSpaceWithOptimalSlots(size_t slotSize);
	static int blocksOccupied(size_t slotSize);
};