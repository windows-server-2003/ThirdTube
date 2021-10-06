#pragma once

#define TASK_SAVE_SETTINGS 0
#define TASK_CHANGE_BRIGHTNESS 1
#define TASK_RELOAD_STRING_RESOURCE 2
#define TASK_SAVE_HISTORY 3
#define TASK_SAVE_SUBSCRIPTION 4

void misc_tasks_request(int type);
void misc_tasks_thread_func(void *);
void misc_tasks_thread_exit_request();
