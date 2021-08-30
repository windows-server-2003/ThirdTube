#include "system/util/async_task.hpp"
#include "headers.hpp"
#include <deque>
#include <utility>


static Handle resource_lock;
static bool resource_lock_initialized = false;
static std::deque<std::pair<AsyncTaskFuncType, void *> > task_queue; // task_queue.front() is the task currently running

static void lock() {
	if (!resource_lock_initialized) {
		svcCreateMutex(&resource_lock, false);
		resource_lock_initialized = true;
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(resource_lock);
}


void remove_all_async_tasks_with_type(AsyncTaskFuncType func) {
	lock();
	for (auto itr = task_queue.begin(); itr != task_queue.end(); ) {
		if (itr != task_queue.begin() && itr->first == func) itr = task_queue.erase(itr);
		else itr++;
	}
	release();
}

void queue_async_task(AsyncTaskFuncType func, void *arg) {
	lock();
	task_queue.push_back({func, arg});
	release();
}

int is_async_task_running(AsyncTaskFuncType func) {
	int res = 0;
	lock();
	for (int i = 0; i < (int) task_queue.size(); i++) if (task_queue[i].first == func) {
		res = i ? 1 : 2;
		break;
	}
	release();
	return res;
}

static bool should_be_running = true;
void async_task_thread_exit_request() { should_be_running = false; }
void async_task_thread_func(void *arg) {
	(void) arg;
	
	while (should_be_running) {
		AsyncTaskFuncType func = NULL;
		void *arg = NULL;
		
		lock();
		if (task_queue.size()) {
			func = task_queue.front().first;
			arg = task_queue.front().second;
		}
		release();
		
		if (func) {
			func(arg);
			
			lock();
			task_queue.pop_front();
			release();
		} else usleep(50000);
	}
	
	threadExit(0);
}
