#include "system/util/misc_tasks.hpp"
#include "system/util/settings.hpp"
#include "system/util/history.hpp"
#include "system/util/change_setting.hpp"
#include "system/util/string_resource.hpp"
#include "headers.hpp"

static bool should_be_running = true;
static bool request[100];

void misc_tasks_request(int type) { request[type] = true; }

void misc_tasks_thread_func(void *arg) {
	(void) arg;
	
	load_watch_history();
	while (should_be_running) {
		if (request[TASK_SAVE_SETTINGS]) {
			request[TASK_SAVE_SETTINGS] = false;
			save_settings();
		} else if (request[TASK_CHANGE_BRIGHTNESS]) {
			request[TASK_CHANGE_BRIGHTNESS] = false;
			Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
		} else if (request[TASK_RELOAD_STRING_RESOURCE]) {
			request[TASK_RELOAD_STRING_RESOURCE] = false;
			load_string_resources(var_lang);
		} else if (request[TASK_SAVE_HISTORY]) {
			request[TASK_SAVE_HISTORY] = false;
			save_watch_history();
		} else usleep(50000);
	}
	
	Util_log_save("misc-task", "Thread exit.");
	threadExit(0);
}
void misc_tasks_thread_exit_request() { should_be_running = false; }
