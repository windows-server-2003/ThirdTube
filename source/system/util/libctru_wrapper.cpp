#include "headers.hpp"

static bool lock_initialized = false;
static Handle resource_lock;

static void lock() {
	if (!lock_initialized) {
		svcCreateMutex(&resource_lock, false);
		lock_initialized = true;
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(resource_lock);
}

void *linearAlloc_concurrent(size_t size) {
	lock();
	void *res = linearAlloc(size);
	release();
	return res;
}
void linearFree_concurrent(void *ptr) {
	lock();
	linearFree(ptr);
	release();
}


void my_assert(bool condition) {
	if (!condition) {
		volatile int *pointer = NULL;
		*pointer = 100;
	}
}
