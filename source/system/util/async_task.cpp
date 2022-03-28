#include "system/util/async_task.hpp"
#include "headers.hpp"
#include <deque>
#include <utility>

static Mutex resource_lock;
static std::deque<std::pair<AsyncTaskFuncType, void *> > task_queue; // task_queue.front() is the task currently running

void remove_all_async_tasks_with_type(AsyncTaskFuncType func) {
	resource_lock.lock();
	for (auto itr = task_queue.begin(); itr != task_queue.end(); ) {
		if (itr != task_queue.begin() && itr->first == func) itr = task_queue.erase(itr);
		else itr++;
	}
	resource_lock.unlock();
}

void queue_async_task(AsyncTaskFuncType func, void *arg) {
	resource_lock.lock();
	task_queue.push_back({func, arg});
	resource_lock.unlock();
}

int is_async_task_running(AsyncTaskFuncType func) {
	int res = 0;
	resource_lock.lock();
	for (int i = 0; i < (int) task_queue.size(); i++) if (task_queue[i].first == func) {
		res = i ? 1 : 2;
		break;
	}
	resource_lock.unlock();
	return res;
}

static bool should_be_running = true;
void async_task_thread_exit_request() { should_be_running = false; }
void async_task_thread_func(void *arg) {
	(void) arg;
	
	while (should_be_running) {
		AsyncTaskFuncType func = NULL;
		void *arg = NULL;
		
		resource_lock.lock();
		if (task_queue.size()) {
			func = task_queue.front().first;
			arg = task_queue.front().second;
		}
		resource_lock.unlock();
		
		if (func) {
			func(arg);
			
			resource_lock.lock();
			task_queue.pop_front();
			resource_lock.unlock();
		} else usleep(50000);
	}
	
	threadExit(0);
}
