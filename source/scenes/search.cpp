#include "headers.hpp"
#include <vector>
#include <numeric>

#include "scenes/search.hpp"
#include "youtube_parser/parser.hpp"

#define RESULT_Y_LOW 20
#define RESULT_Y_HIGH 220
#define FONT_VERTICAL_INTERVAL 15


namespace Search {
	std::string string_resource[DEF_SEARCH_NUM_OF_MSG];
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	Handle resource_lock;
	std::string cur_search_word = "CITRUS";
	bool search_request = false;
	YouTubeSearchResult search_result;
	Thread search_thread;
	
	int scroll_offset = 0;
	int scroll_max = 0;
	
	
	int last_touch_x = -1;
	int last_touch_y = -1;
	int first_touch_x = -1;
	int first_touch_y = -1;
	bool list_grabbed = false;
	bool list_scrolling = false;
};
using namespace Search;


static void reset_hid_state() {
	last_touch_x = last_touch_y = -1;
	first_touch_x = first_touch_y = -1;
	list_scrolling = false;
	list_grabbed = false;
}

static void send_search_request(std::string search_word) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	cur_search_word = search_word;
	search_request = true;
	search_result = YouTubeSearchResult(); // reset to empty results
	reset_hid_state();
	scroll_max = 0;
	scroll_offset = 0;
	svcReleaseMutex(resource_lock);
}

static void search_thread_func(void* arg) {
	(void) arg;
	
	while (!exiting) {
		if (search_request) {
			std::string search_url = "https://m.youtube.com/results?search_query=";
			for (auto c : cur_search_word) {
				search_url.push_back('%');
				search_url.push_back("0123456789ABCDEF"[(u8) c / 16]);
				search_url.push_back("0123456789ABCDEF"[(u8) c % 16]);
			}
			add_cpu_limit(25);
			YouTubeSearchResult new_result = parse_search(search_url);
			remove_cpu_limit(25);
			
			svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
			search_result = new_result;
			svcReleaseMutex(resource_lock);
			
			scroll_offset = 0;
			scroll_max = std::max(0, (int) new_result.results.size() * FONT_VERTICAL_INTERVAL - (RESULT_Y_HIGH - RESULT_Y_LOW));
			reset_hid_state();
			
			search_request = false;
		} else usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);
		
		while (thread_suspend)
			usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
	}
	
	threadExit(0);
}

bool Search_query_init_flag(void) {
	return already_init;
}


void Search_resume(std::string arg)
{
	(void) arg;
	reset_hid_state();
	thread_suspend = false;
	var_need_reflesh = true;
}

void Search_suspend(void)
{
	thread_suspend = true;
}

void Search_init(void)
{
	Util_log_save("search/init", "Initializing...");
	bool new_3ds = false;
	Result_with_string result;
	
	svcCreateMutex(&resource_lock, false);
	
	reset_hid_state();
	scroll_offset = 0;
	scroll_max = 0;
	
	// result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SEARCH_NUM_OF_MSG);
	// Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	search_thread = threadCreate(search_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	Search_resume("");
	already_init = true;
}

void Search_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	u64 time_out = 10000000000;
	
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(search_thread, time_out));
	threadFree(search_thread);

	Util_log_save("search/exit", "Exited.");
}

static void draw_search_result(const YouTubeSearchResult &result, Hid_info key) {
	if (result.results.size()) {
		for (size_t i = 0; i < result.results.size(); i++) {
			int y_l = RESULT_Y_LOW + i * FONT_VERTICAL_INTERVAL - scroll_offset;
			int y_r = RESULT_Y_LOW + (i + 1) * FONT_VERTICAL_INTERVAL - scroll_offset;
			if (y_r <= RESULT_Y_LOW || y_l >= RESULT_Y_HIGH) continue;
			
			if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r && list_grabbed && !list_scrolling) {
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 0, y_l, 320, FONT_VERTICAL_INTERVAL);
			}
			
			auto cur_result = result.results[i];
			Draw(cur_result.title, 0, y_l, 0.5, 0.5, DEF_DRAW_BLACK);
		}
	} else if (search_request) {
		Draw("Loading", 0, RESULT_Y_LOW, 0.5, 0.5, DEF_DRAW_BLACK);
		
	} else Draw("Empty", 0, RESULT_Y_LOW, 0.5, 0.5, DEF_DRAW_BLACK);
}

Intent Search_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	int color = DEF_DRAW_BLACK;
	int back_color = DEF_DRAW_WHITE;
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	if (var_night_mode)
	{
		color = DEF_DRAW_WHITE;
		back_color = DEF_DRAW_BLACK;
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto search_result_bak = search_result;
	svcReleaseMutex(resource_lock);

	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, back_color);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();

		Draw_screen_ready(1, back_color);
		
		// (!) : I don't know how to draw textures truncated, so I will just fill the margin with white again
		draw_search_result(search_result_bak, key);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 0, 0, 320, RESULT_Y_LOW - 1);
		Draw_texture(var_square_image[0], DEF_DRAW_BROWN, 0, RESULT_Y_LOW - 1, 320, 1);
		Draw_texture(var_square_image[0], DEF_DRAW_BROWN, 0, RESULT_Y_HIGH, 320, 1);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 0, RESULT_Y_HIGH + 1, 320, 240 - RESULT_Y_HIGH - 1);

		// Draw(DEF_SAPP0_VER, 0, 0, 0.4, 0.4, DEF_DRAW_GREEN);
		
		Draw("Search scene (Press A to search)", 0, 0, 0.5, 0.5, DEF_DRAW_BLACK);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_bot_ui();
		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
		reset_hid_state();
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
		reset_hid_state();
	} else {
		if (key.p_touch) {
			first_touch_x = key.touch_x;
			first_touch_y = key.touch_y;
			if (first_touch_y >= RESULT_Y_LOW && first_touch_y < RESULT_Y_HIGH && var_afk_time <= var_time_to_turn_off_lcd) list_grabbed = true;
		} else if (list_grabbed && !list_scrolling && key.touch_y != -1 && std::abs(key.touch_y - first_touch_y) >= 4) {
			list_scrolling = true;
			scroll_offset += first_touch_y - key.touch_y;
			if (scroll_offset < 0) scroll_offset = 0;
			if (scroll_offset > scroll_max) scroll_offset = scroll_max;
		} else if (list_scrolling && key.touch_y != -1) {
			scroll_offset += last_touch_y - key.touch_y;
			if (scroll_offset < 0) scroll_offset = 0;
			if (scroll_offset > scroll_max) scroll_offset = scroll_max;
		}
		
		bool ignore = false;
		if (key.p_start || (key.p_touch && key.touch_x >= 110 && key.touch_x <= 230 && key.touch_y >= 220 && key.touch_y <= 240)) {
			// TODO : implement confirm dialog
			intent.next_scene = SceneType::EXIT;
			ignore = true;
		}
		if (!ignore && key.touch_y == -1 && last_touch_y != -1 && first_touch_x != -1) {
			if (last_touch_y + scroll_offset >= 20 && (size_t) last_touch_y + scroll_offset < 20 + search_result_bak.results.size() * FONT_VERTICAL_INTERVAL) {
				if (!list_scrolling && list_grabbed) {
					int index = (last_touch_y + scroll_offset - 20) / FONT_VERTICAL_INTERVAL;
					intent.next_scene = SceneType::VIDEO_PLAYER;
					intent.arg = search_result_bak.results[index].url;
					ignore = true;
				}
			}
		}
		if (!ignore) {
			if (last_touch_y != -1 && key.touch_y != -1) { // scroll
				
			}
			if (key.p_a) {
				if (!search_request) { // input video id
					SwkbdState keyboard;
					swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 11);
					swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY | SWKBD_PREDICTIVE_INPUT);
					swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
					swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
					swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "OK", true);
					swkbdSetInitialText(&keyboard, cur_search_word.c_str());
					char search_word[128];
					add_cpu_limit(50);
					auto button_pressed = swkbdInputText(&keyboard, search_word, 32);
					remove_cpu_limit(50);
					if (button_pressed == SWKBD_BUTTON_RIGHT) send_search_request(search_word);
				}
				
				/*
				intent.next_scene = SceneType::VIDEO_PLAYER;
				var_need_reflesh = true;*/
			} else if(key.h_touch || key.p_touch)
				var_need_reflesh = true;
		}
		
		if (key.touch_y == -1) list_scrolling = list_grabbed = false;
		last_touch_x = key.touch_x;
		last_touch_y = key.touch_y;
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
