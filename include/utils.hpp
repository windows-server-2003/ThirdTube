#pragma once

#include <vector>

/*
	queue with a fixed capacity
	basic idea :
		hold the head index and the tail index of the actual data
		when the head would go beyond the end of the buffer, cycle it to the beginning of the buffer (same for tail)
	thread-safe unless :
		two threads both call push at the same time
		two threads both call pop at the same time
		two threads both call push and semi_clear
*/
template<typename T> class fixed_capacity_queue {
private :
	std::vector<T> buffer;
	volatile size_t head = 0; // the index of the element in the buffer which the next pushed element should go in
	volatile size_t tail = 0; // the index of the element in the buffer which should be poped next
public :
	size_t capacity = 0;
	fixed_capacity_queue () = default;
	// if buffer.size() == capacity, we cannot distinguish between the empty state and the full state, so allocate with additional one element
	fixed_capacity_queue (size_t capacity) : buffer(capacity + 1), head(0), tail(0), capacity(capacity)
	{
		Util_log_save("que", "contruct:" + std::to_string(capacity));
		
	}
	// get the size of the queue
	size_t size()
	{
		if (head >= tail) return head - tail;
		else return head + capacity + 1 - tail;
	}
	bool full()
	{
		return size() == capacity;
	}
	bool empty()
	{
		return size() == 0;
	}
	// push an element
	// returns if it was successful (i.e if the queue was not full)
	bool push(const T &element) {
		if (full()) return false;
		buffer[head] = element;
		head = (head == capacity ? 0 : head + 1);
		return true;
	}
	// pop an element and store the pop element in 'target'
	// returns if it was successful (i.e if the queue was not empty)
	bool pop(T &target) {
		if (empty()) return false;
		target = buffer[tail];
		tail = (tail == capacity ? 0 : tail + 1);
		return true;
	}
	// clear the queue, return the erased elements
	// must not be used when other functions may be called by another thread
	std::vector<T> clear() {
		if (head == tail) return {};
		std::vector<T> res;
		if (head > tail) res = std::vector<T>(buffer.begin() + tail, buffer.begin() + head);
		else
		{
			res = std::vector<T>(buffer.begin() + tail, buffer.end());
			res.insert(res.end(), buffer.begin(), buffer.begin() + head);
		}
		head = tail;
		return res;
	}
};
