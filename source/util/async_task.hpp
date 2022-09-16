#pragma once

using AsyncTaskFuncType = void (*) (void *);

// remove all tasks where the specified function is to be run
void remove_all_async_tasks_with_type(AsyncTaskFuncType func);

// add a new task
void queue_async_task(AsyncTaskFuncType func, void *arg);

// check if a task with the specified function is queued and/or running
// 0 : not running
// 1 : queued
// 2 : running
int is_async_task_running(AsyncTaskFuncType func);

void async_task_thread_exit_request();
void async_task_thread_func(void *arg); // a thread running this function should be created 
