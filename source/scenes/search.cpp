#include "headers.hpp"
#include <vector>
#include <numeric>

#include "scenes/search.hpp"
#include "youtube_parser/parser.hpp"

#define RESULT_Y_LOW 20
#define RESULT_Y_HIGH 240
#define FONT_VERTICAL_INTERVAL 60
#define THUMBNAIL_HEIGHT 54
#define THUMBNAIL_WIDTH 96
#define LOAD_MORE_HEIGHT 30


namespace Search {
	std::string string_resource[DEF_SEARCH_NUM_OF_MSG];
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	Handle resource_lock;
	std::string cur_search_word = "CITRUS";
	bool search_request = false;
	bool load_more_request = false; // 
	YouTubeSearchResult search_result;
	std::vector<std::vector<std::string> > wrapped_titles;
	Thread search_thread;
	
	int scroll_offset = 0;
	
	
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
	load_more_request = false;
	for (auto i : search_result.results) {
		if (i.type == YouTubeSearchResult::Item::VIDEO) cancel_request_thumbnail(i.video.thumbnail_url);
	}
	search_result = YouTubeSearchResult(); // reset to empty results
	reset_hid_state();
	scroll_offset = 0;
	svcReleaseMutex(resource_lock);
}
static void send_load_more_request() {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (search_result.results.size()) { // you can't load more items on empty search results
		load_more_request = true;
	}
	svcReleaseMutex(resource_lock);
}


// truncate and wrap into two lines if necessary
static std::vector<std::string> truncate_str(std::string input_str, int max_width, double x_size, double y_size) {
	if (input_str == "") return {""};
	
	std::vector<std::string> input(128);
	{
		int out_num;
		Exfont_text_parse(input_str, &input[0], 128, &out_num);
		input.resize(out_num);
	}
	if (!input.size()) return {""};
	std::vector<std::vector<std::string> > words; // each word is considered not separable
	for (size_t i = 0; i < input.size(); i++) {
		bool seperate;
		if (!i) seperate = true;
		else {
			std::string last_char = words.back().back();
			seperate = last_char.size() != 1 || input[i].size() != 1 || last_char == " " || input[i] == " ";
		}
		if (seperate) words.push_back({input[i]});
		else words.back().push_back(input[i]);
	}
	
	int n = words.size();
	int first_line_word_num = 0;
	{ // binary search the number of words that fit in the first line
		int l = 0;
		int r = n + 1;
		while (r - l > 1) {
			int m = l + ((r - l) >> 1);
			std::string query_text;
			for (int i = 0; i < m; i++) for (auto j : words[i]) query_text += j;
			if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
			else r = m;
		}
		first_line_word_num = l;
	}
	std::string tmp;
	for (auto i : words) {
		for (auto j : i) tmp += j;
		tmp += " | ";
	}
	
	if (!first_line_word_num) { // can't even accommodate the first word -> partially display the word and add "..."
		std::vector<std::string> first_word = words[0];
		int l = 0;
		int r = first_word.size();
		while (r - l > 1) { // binary search the number of characters that fit in the first line
			int m = l + ((r - l) >> 1);
			std::string query_text;
			for (int i = 0; i < m; i++) query_text += first_word[i];
			query_text += "...";
			if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
			else r = m;
		}
		std::string first_line;
		for (int i = 0; i < l; i++) first_line += first_word[i];
		first_line += "...";
		return {first_line};
	} else {
		std::string first_line;
		for (int i = 0; i < first_line_word_num; i++) for (auto j : words[i]) first_line += j;
		words.erase(words.begin(), words.begin() + first_line_word_num);
		if (!words.size()) return {first_line}; // the first line accommodated the entire string
		std::vector<std::string> remaining_str; // ignore the word unit from here
		for (auto i : words) for (auto j : i) remaining_str.push_back(j);
		
		// check if the entire remaining string fit in the second line
		{
			std::string tmp_str;
			for (auto i : remaining_str) tmp_str += i;
			if (Draw_get_width(tmp_str, x_size, y_size) <= max_width) return {first_line, tmp_str};
		}
		// binary search the number of words that fit in the second line with "..."
		int l = 0;
		int r = remaining_str.size();
		while (r - l > 1) {
			int m = l + ((r - l) >> 1);
			std::string query_text;
			for (int i = 0; i < m; i++) query_text += remaining_str[i];
			query_text += "...";
			if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
			else r = m;
		}
		std::string second_line;
		for (int i = 0; i < l; i++) second_line += remaining_str[i];
		second_line += "...";
		return {first_line, second_line};
	}
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
			YouTubeSearchResult new_result = youtube_parse_search(search_url);
			remove_cpu_limit(25);
			
			// wrap and truncate here
			Util_log_save("search", "truncate start");
			std::vector<std::vector<std::string> > new_wrapped_titles(new_result.results.size());
			for (size_t i = 0; i < new_result.results.size(); i++) {
				std::string cur_str = new_result.results[i].type == YouTubeSearchResult::Item::VIDEO ? new_result.results[i].video.title : new_result.results[i].channel.name;
				new_wrapped_titles[i] = truncate_str(cur_str, 320 - (THUMBNAIL_WIDTH + 3), 0.5, 0.5);
			}
			Util_log_save("search", "truncate end");
			
			
			svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
			search_result = new_result;
			wrapped_titles = new_wrapped_titles;
			for (auto i : search_result.results)
				if (i.type == YouTubeSearchResult::Item::VIDEO) request_thumbnail(i.video.thumbnail_url);
			svcReleaseMutex(resource_lock);
			
			scroll_offset = 0;
			reset_hid_state();
			
			search_request = false;
		} else if (load_more_request) {
			auto new_result = youtube_continue_search(search_result);
			
			// if another search was made and load_more_request was thus disabled, we should not modify the cleared result
			if (load_more_request) {
				// wrap and truncate here
				Util_log_save("search", "truncate start");
				std::vector<std::vector<std::string> > new_wrapped_titles(new_result.results.size());
				for (size_t i = 0; i < new_result.results.size(); i++) {
					std::string cur_str = new_result.results[i].type == YouTubeSearchResult::Item::VIDEO ? new_result.results[i].video.title : new_result.results[i].channel.name;
					new_wrapped_titles[i] = truncate_str(cur_str, 320 - (THUMBNAIL_WIDTH + 3), 0.5, 0.5);
				}
				Util_log_save("search", "truncate end");
				
				svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
				if (load_more_request) {
					if (new_result.error != "") search_result.error = new_result.error;
					else {
						for (size_t i = search_result.results.size(); i < new_result.results.size(); i++)
							if (new_result.results[i].type == YouTubeSearchResult::Item::VIDEO) request_thumbnail(new_result.results[i].video.thumbnail_url);
						search_result = new_result;
						wrapped_titles = new_wrapped_titles;
					}
				}
				svcReleaseMutex(resource_lock);
			}
			load_more_request = false;
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
	Result_with_string result;
	
	svcCreateMutex(&resource_lock, false);
	
	reset_hid_state();
	scroll_offset = 0;
	search_result = YouTubeSearchResult();
	search_request = false;
	load_more_request = false;
	
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


static void draw_search_result(const YouTubeSearchResult &result, const std::vector<std::vector<std::string> > &wrapped_titles, Hid_info key, int color) {
	if (result.results.size()) {
		for (size_t i = 0; i < result.results.size(); i++) {
			int y_l = RESULT_Y_LOW + i * FONT_VERTICAL_INTERVAL - scroll_offset;
			int y_r = RESULT_Y_LOW + (i + 1) * FONT_VERTICAL_INTERVAL - scroll_offset;
			if (y_r <= RESULT_Y_LOW || y_l >= RESULT_Y_HIGH) continue;
			
			if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r && list_grabbed && !list_scrolling) {
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 0, y_l, 320, FONT_VERTICAL_INTERVAL);
			}
			
			if (result.results[i].type == YouTubeSearchResult::Item::VIDEO) {
				auto cur_video = result.results[i].video;
				// title
				auto title_lines = wrapped_titles[i];
				for (size_t line = 0; line < title_lines.size(); line++) {
					Draw(title_lines[line], THUMBNAIL_WIDTH + 3, y_l + line * 13, 0.5, 0.5, color);
				}
				// Draw(std::to_string(Draw_get_width(cur_video.title, 0.5, 0.5)), THUMBNAIL_WIDTH + 3, y_l + 10, 0.5, 0.5, color);
				// thumbnail
				draw_thumbnail(cur_video.thumbnail_url, 0, y_l, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
			} else if (result.results[i].type == YouTubeSearchResult::Item::CHANNEL) {
				auto cur_channel = result.results[i].channel;
				Draw("channel : " + cur_channel.name, 0, y_l, 0.5, 0.5, color);
			}
		}
		// draw load-more margin
		{
			std::string draw_str = "";
			if (load_more_request) draw_str = "Loading...";
			else if (result.error != "") draw_str = result.error;
			
			int y = RESULT_Y_LOW + result.results.size() * FONT_VERTICAL_INTERVAL - scroll_offset;
			if (y < RESULT_Y_HIGH) {
				int width = Draw_get_width(draw_str, 0.5, 0.5);
				Draw(draw_str, (320 - width) / 2, y, 0.5, 0.5, color);
			}
		}
	} else if (search_request) {
		Draw("Loading", 0, RESULT_Y_LOW, 0.5, 0.5, color);
		
	} else Draw("Empty", 0, RESULT_Y_LOW, 0.5, 0.5, color);
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
	auto wrapped_titles_bak = wrapped_titles;
	svcReleaseMutex(resource_lock);

	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, back_color);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		Draw("Press START to exit the app", 0, 225, 0.5, 0.5, color);

		Draw_screen_ready(1, back_color);
		
		// (!) : I don't know how to draw textures truncated, so I will just fill the margin with white again
		draw_search_result(search_result_bak, wrapped_titles_bak, key, color);
		Draw_texture(var_square_image[0], back_color, 0, 0, 320, RESULT_Y_LOW - 1);
		Draw_texture(var_square_image[0], color, 0, RESULT_Y_LOW - 1, 320, 1);
		Draw_texture(var_square_image[0], back_color, 0, RESULT_Y_HIGH + 1, 320, 240 - RESULT_Y_HIGH - 1);

		// Draw(DEF_SAPP0_VER, 0, 0, 0.4, 0.4, DEF_DRAW_GREEN);
		
		Draw("Search scene (Press A to search)", 0, 0, 0.5, 0.5, color);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

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
		int scroll_max = std::max(0, (int) search_result.results.size() * FONT_VERTICAL_INTERVAL - (RESULT_Y_HIGH - RESULT_Y_LOW) + LOAD_MORE_HEIGHT);
		if (key.p_touch) {
			first_touch_x = key.touch_x;
			first_touch_y = key.touch_y;
			if (first_touch_y >= RESULT_Y_LOW && first_touch_y < std::min<s32>(search_result_bak.results.size() * FONT_VERTICAL_INTERVAL, RESULT_Y_HIGH) &&
				var_afk_time <= var_time_to_turn_off_lcd) list_grabbed = true;
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
		if (key.p_start) {
			// TODO : implement confirm dialog
			intent.next_scene = SceneType::EXIT;
			ignore = true;
		}
		if (!ignore && key.touch_y == -1 && last_touch_y != -1 && first_touch_x != -1) {
			if (last_touch_y + scroll_offset >= 20 && (size_t) last_touch_y + scroll_offset < 20 + search_result_bak.results.size() * FONT_VERTICAL_INTERVAL) {
				if (!list_scrolling && list_grabbed) {
					int index = (last_touch_y + scroll_offset - 20) / FONT_VERTICAL_INTERVAL;
					if (search_result_bak.results[index].type == YouTubeSearchResult::Item::VIDEO) {
						intent.next_scene = SceneType::VIDEO_PLAYER;
						intent.arg = search_result_bak.results[index].video.url;
						ignore = true;
					} else {} // TODO : implement
				}
			}
		}
		if (!ignore) {
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
			} else if (RESULT_Y_LOW + search_result.results.size() * FONT_VERTICAL_INTERVAL - scroll_offset < RESULT_Y_HIGH && !load_more_request) {
				send_load_more_request();
			} else if(key.h_touch || key.p_touch)
				var_need_reflesh = true;
		}
		if (!ignore && key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
		
		if (key.touch_y == -1) list_scrolling = list_grabbed = false;
		last_touch_x = key.touch_x;
		last_touch_y = key.touch_y;
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
