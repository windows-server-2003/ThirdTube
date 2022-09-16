#pragma once
#include <3ds.h>

void *linearAlloc_concurrent(size_t size);
void linearFree_concurrent(void *ptr);

class Mutex {
private :
	LightLock mutex_ = 1; // unlocked mutex contains 1
public :
	Mutex () = default;
	// non-copiable, non-movable because the memory address of `mutex_` must never change
	Mutex (const Mutex &) = delete;
	Mutex & operator = (const Mutex &) = delete;
	Mutex (const Mutex &&) = delete;
	Mutex & operator = (const Mutex &&) = delete;
	
	void lock() { LightLock_Lock(&mutex_); }
	void unlock() { LightLock_Unlock(&mutex_); }
};

void my_assert(bool condition); // causes a data abort
