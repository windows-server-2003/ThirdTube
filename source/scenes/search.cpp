#include "headers.hpp"
#include <vector>
#include <numeric>

#include "scenes/search.hpp"
#include "youtube_parser/parser.hpp"

#define RESULT_Y_LOW 20
#define RESULT_Y_HIGH 240
#define RESULTS_VERTICAL_INTERVAL 60
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
	YouTubeSearchResult search_result;
	std::vector<std::vector<std::string> > wrapped_titles;
	Thread search_thread;
	
	VerticalScroller results_scroller = VerticalScroller(0, 320, RESULT_Y_LOW, RESULT_Y_HIGH);
};
using namespace Search;

static void on_search_complete() {
	results_scroller.reset();
}

static bool send_search_request(std::string search_word) {
	if (!is_webpage_loading_requested(LoadRequestType::SEARCH)) {
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		cur_search_word = search_word;
		for (auto i : search_result.results) {
			if (i.type == YouTubeSearchResult::Item::VIDEO) cancel_request_thumbnail(i.video.thumbnail_url);
			else if (i.type == YouTubeSearchResult::Item::CHANNEL) cancel_request_thumbnail(i.channel.icon_url);
		}
		search_result = YouTubeSearchResult(); // reset to empty results
		results_scroller.reset();
		
		SearchRequestArg *request = new SearchRequestArg;
		request->lock = resource_lock;
		request->result = &search_result;
		request->search_word = search_word;
		request->max_width = 320 - (THUMBNAIL_WIDTH + 3);
		request->text_size_x = 0.5;
		request->text_size_y = 0.5;
		request->wrapped_titles = &wrapped_titles;
		request->on_load_complete = on_search_complete;
		request_webpage_loading(LoadRequestType::SEARCH, request);
		
		svcReleaseMutex(resource_lock);
		return true;
	} else return false;
}
static bool send_load_more_request() {
	bool res = false;
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (search_result.results.size()) { // you can't load more items on empty search results
		SearchRequestArg *request = new SearchRequestArg;
		request->lock = resource_lock;
		request->result = &search_result;
		request->max_width = 320 - (THUMBNAIL_WIDTH + 3);
		request->text_size_x = 0.5;
		request->text_size_y = 0.5;
		request->wrapped_titles = &wrapped_titles;
		request->on_load_complete = NULL; // we don't have to reset hid/scroll state after load-more
		request_webpage_loading(LoadRequestType::SEARCH_CONTINUE, request);
		res = true;
	}
	svcReleaseMutex(resource_lock);
	return res;
}



bool Search_query_init_flag(void) {
	return already_init;
}


void Search_resume(std::string arg)
{
	(void) arg;
	results_scroller.on_resume();
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
	
	results_scroller.reset();
	search_result = YouTubeSearchResult();
	
	// result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SEARCH_NUM_OF_MSG);
	// Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	Search_resume("");
	already_init = true;
}

void Search_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	u64 time_out = 10000000000;
	
	Util_log_save("search/exit", "Exited.");
}


static void draw_search_result(const YouTubeSearchResult &result, const std::vector<std::vector<std::string> > &wrapped_titles, Hid_info key, int color) {
	if (result.results.size()) {
		for (size_t i = 0; i < result.results.size(); i++) {
			int y_l = RESULT_Y_LOW + i * RESULTS_VERTICAL_INTERVAL - results_scroller.get_offset();
			int y_r = y_l + RESULTS_VERTICAL_INTERVAL;
			if (y_r <= RESULT_Y_LOW || y_l >= RESULT_Y_HIGH) continue;
			
			if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r && results_scroller.is_selecting()) {
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 0, y_l, 320, RESULTS_VERTICAL_INTERVAL);
			}
			
			if (result.results[i].type == YouTubeSearchResult::Item::VIDEO) {
				auto cur_video = result.results[i].video;
				// title
				auto title_lines = wrapped_titles[i];
				for (size_t line = 0; line < title_lines.size(); line++) {
					Draw(title_lines[line], THUMBNAIL_WIDTH + 3, y_l + line * 13, 0.5, 0.5, color);
				}
				// thumbnail
				draw_thumbnail(cur_video.thumbnail_url, 0, y_l, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
			} else if (result.results[i].type == YouTubeSearchResult::Item::CHANNEL) {
				auto cur_channel = result.results[i].channel;
				Draw(cur_channel.name, THUMBNAIL_WIDTH + 3, y_l, 0.5, 0.5, color);
				// icon
				draw_thumbnail(cur_channel.icon_url, (THUMBNAIL_WIDTH - THUMBNAIL_HEIGHT) / 2, y_l, THUMBNAIL_HEIGHT, THUMBNAIL_HEIGHT);
			}
		}
		// draw load-more margin
		{
			std::string draw_str = "";
			if (is_webpage_loading_requested(LoadRequestType::SEARCH_CONTINUE)) draw_str = "Loading...";
			else if (result.error != "") draw_str = result.error;
			
			int y = RESULT_Y_LOW + result.results.size() * RESULTS_VERTICAL_INTERVAL - results_scroller.get_offset();
			if (y < RESULT_Y_HIGH) {
				int width = Draw_get_width(draw_str, 0.5, 0.5);
				Draw(draw_str, (320 - width) / 2, y, 0.5, 0.5, color);
			}
		}
	} else if (is_webpage_loading_requested(LoadRequestType::SEARCH)) {
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
		results_scroller.on_resume();
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
		results_scroller.on_resume();
	} else {
		auto released_point = results_scroller.update(key, search_result_bak.results.size() ? search_result_bak.results.size() * RESULTS_VERTICAL_INTERVAL + LOAD_MORE_HEIGHT : 0);
		
		bool ignore = false;
		if (key.p_start) {
			// TODO : implement confirm dialog
			intent.next_scene = SceneType::EXIT;
			ignore = true;
		}
		if (!ignore && released_point.first != -1) {
			int released_y = released_point.second;
			if (released_y < (int) search_result_bak.results.size() * RESULTS_VERTICAL_INTERVAL) {
				int index = released_y / RESULTS_VERTICAL_INTERVAL;
				if (search_result_bak.results[index].type == YouTubeSearchResult::Item::VIDEO) {
					intent.next_scene = SceneType::VIDEO_PLAYER;
					intent.arg = search_result_bak.results[index].video.url;
					ignore = true;
				} else if (search_result_bak.results[index].type == YouTubeSearchResult::Item::CHANNEL) {
					intent.next_scene = SceneType::CHANNEL;
					intent.arg = search_result_bak.results[index].channel.url;
					ignore = true;
				}
			}
		}
		if (!ignore) {
			if (key.p_a) {
				if (!is_webpage_loading_requested(LoadRequestType::SEARCH)) { // input video id
					SwkbdState keyboard;
					swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 11);
					swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY | SWKBD_PREDICTIVE_INPUT);
					swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
					swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
					swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "OK", true);
					swkbdSetInitialText(&keyboard, cur_search_word.c_str());
					char search_word[128];
					add_cpu_limit(40);
					auto button_pressed = swkbdInputText(&keyboard, search_word, 32);
					remove_cpu_limit(40);
					if (button_pressed == SWKBD_BUTTON_RIGHT) send_search_request(search_word);
				}
			} else if (RESULT_Y_LOW + search_result_bak.results.size() * RESULTS_VERTICAL_INTERVAL - results_scroller.get_offset() < RESULT_Y_HIGH &&
				!is_webpage_loading_requested(LoadRequestType::SEARCH_CONTINUE)) {
				
				send_load_more_request();
			} else if(key.h_touch || key.p_touch)
				var_need_reflesh = true;
		}
		if (!ignore && key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
