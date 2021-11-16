#pragma once

void *linearAlloc_concurrent(size_t size);
void linearFree_concurrent(void *ptr);

void my_assert(bool condition); // causes a data abort
