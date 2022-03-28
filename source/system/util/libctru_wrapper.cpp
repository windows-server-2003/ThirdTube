#include "headers.hpp"

static Mutex resource_lock;

void *linearAlloc_concurrent(size_t size) {
	resource_lock.lock();
	void *res = linearAlloc(size);
	resource_lock.unlock();
	return res;
}
void linearFree_concurrent(void *ptr) {
	resource_lock.lock();
	linearFree(ptr);
	resource_lock.unlock();
}


void my_assert(bool condition) {
	if (!condition) {
		volatile int *pointer = NULL;
		*pointer = 100;
	}
}
