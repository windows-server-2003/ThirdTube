#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/search.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "ui/ui.hpp"
#include "network/thumbnail_loader.hpp"
#include "network/network_io.hpp"
#include "system/util/async_task.hpp"

#define SEARCH_BOX_MARGIN 4

#define URL_BUTTON_WIDTH 60

#define MAX_THUMBNAIL_LOAD_REQUEST 20


namespace Search {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	Handle resource_lock;
	std::string cur_search_word = "";
	YouTubeSearchResult search_result;
	bool search_done = false;
	
	std::string last_url_input = "https://m.youtube.com/watch?v=";
	
	bool search_request = false;
	bool url_input_request = false;
	bool clicked_is_channel;
	std::string clicked_url;
	
	YouTubePageType url_jump_request_type = YouTubePageType::INVALID;
	std::string url_jump_request;
	TextView *toast_view;
	int toast_view_visible_frames_left = 0;
	
	const int RESULT_Y_LOW = 25;
	int RESULT_Y_HIGH = 240; // changes according to whether the video playing bar is drawn or not
	
	HorizontalListView *top_bar_view;
	TextView *search_box_view;
	TextView *url_button_view;
	
	VerticalListView *result_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST);
	View *result_bottom_view = new EmptyView(0, 0, 320, 0);
	ScrollView *result_view;
};
using namespace Search;


static void load_search_results(void *);
static void load_more_search_results(void *);

void Search_init(void) {
	Util_log_save("search/init", "Initializing...");
	Result_with_string result;
	
	svcCreateMutex(&resource_lock, false);
	
	search_box_view = (new TextView(0, SEARCH_BOX_MARGIN, 320 - SEARCH_BOX_MARGIN * 3 - URL_BUTTON_WIDTH, RESULT_Y_LOW - SEARCH_BOX_MARGIN * 2));
	search_box_view->set_text_offset(0, -1);
	search_box_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SEARCH_HINT); });
	search_box_view->set_background_color(LIGHT0_BACK_COLOR);
	search_box_view->set_get_text_color([] () { return LIGHT1_TEXT_COLOR; });
	search_box_view->set_on_view_released([] (View &) { search_request = true; });
	
	url_button_view = (new TextView(0, SEARCH_BOX_MARGIN, URL_BUTTON_WIDTH, RESULT_Y_LOW - SEARCH_BOX_MARGIN * 2));
	url_button_view->set_text_offset(0, -1);
	url_button_view->set_x_centered(true);
	url_button_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(URL); });
	url_button_view->set_background_color(LIGHT1_BACK_COLOR);
	url_button_view->set_on_view_released([] (View &) { url_input_request = true; });
	
	toast_view = (new TextView((320 - 150) / 2, 190, 150, DEFAULT_FONT_INTERVAL + SMALL_MARGIN));
	toast_view->set_is_visible(false);
	toast_view->set_x_centered(true);
	toast_view->set_text_offset(0, -1);
	toast_view->set_get_background_color([] (const View &) { return 0x50000000; });
	toast_view->set_get_text_color([] () { return (u32) -1; });
	
	top_bar_view = (new HorizontalListView(0, 0, RESULT_Y_LOW))
		->set_views({
			new EmptyView(0, 0, SEARCH_BOX_MARGIN, RESULT_Y_LOW),
			search_box_view,
			new EmptyView(0, 0, SEARCH_BOX_MARGIN, RESULT_Y_LOW),
			url_button_view
		});
	
	result_view = (new ScrollView(0, 0, 320, RESULT_Y_HIGH - RESULT_Y_LOW))
		->set_views({result_list_view, result_bottom_view});
	
	
	
	Search_resume("");
	already_init = true;
}

void Search_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	
	top_bar_view->recursive_delete_subviews();
	delete top_bar_view;
	top_bar_view = NULL;
	result_view->recursive_delete_subviews();
	delete result_view;
	result_view = NULL;
	toast_view->recursive_delete_subviews();
	delete toast_view;
	toast_view = NULL;
	
	search_box_view = NULL;
	url_button_view = NULL;
	result_list_view = NULL;
	result_bottom_view = NULL;
	
	svcReleaseMutex(resource_lock);
	
	
	Util_log_save("search/exit", "Exited.");
}

void Search_suspend(void) {
	thread_suspend = true;
}

void Search_resume(std::string arg) {
	(void) arg;
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}


// async functions
static void set_loading_bottom_view() {
	delete result_bottom_view;
	result_bottom_view = (new TextView(0, 0, 320, 30))
		->set_text((std::function<std::string ()>) [] () { return LOCALIZED(LOADING); })
		->set_x_centered(true);
	result_view->views[1] = result_bottom_view;
}
static void update_result_bottom_view() {
	delete result_bottom_view;
	if (search_result.error != "" || search_result.has_continue()) {
		TextView *cur_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))->set_x_centered(true);
		if (search_result.error != "") cur_view->set_text(search_result.error);
		else cur_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(LOADING); });
		result_bottom_view = cur_view;
	} else result_bottom_view = new EmptyView(0, 0, 320, 0);
	result_view->views[1] = result_bottom_view;
	result_view->set_on_child_drawn(1, [] (const ScrollView &, int) {
		if (search_result.has_continue() && search_result.error == "") {
			if (!is_async_task_running(load_search_results) &&
				!is_async_task_running(load_more_search_results)) queue_async_task(load_more_search_results, NULL);
		}
	});
}
static View *result_item_to_view(const YouTubeSuccinctItem &item) {
	View *res_view;
	if (item.type == YouTubeSuccinctItem::CHANNEL) {
		SuccinctChannelView *cur_view = new SuccinctChannelView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT);
		cur_view->set_name(item.channel.name);
		cur_view->set_auxiliary_lines({item.channel.subscribers, item.channel.video_num});
		cur_view->set_thumbnail_url(item.channel.icon_url);
		res_view = cur_view;
	} else {
		SuccinctVideoView *cur_view = new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT);
		cur_view->set_title_lines(truncate_str(item.get_name(), 320 - (VIDEO_LIST_THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5));
		if (item.type == YouTubeSuccinctItem::VIDEO) {
			cur_view->set_auxiliary_lines({item.video.views_str, item.video.publish_date});
			cur_view->set_bottom_right_overlay(item.video.duration_text);
		} else if (item.type == YouTubeSuccinctItem::PLAYLIST) {
			cur_view->set_auxiliary_lines({item.playlist.video_count_str});
		}
		cur_view->set_thumbnail_url(item.get_thumbnail_url());
		cur_view->set_is_playlist(item.type == YouTubeSuccinctItem::PLAYLIST);
		res_view = cur_view;
	}
	res_view->set_get_background_color(View::STANDARD_BACKGROUND);
	res_view->set_on_view_released([item] (View &view) {
		clicked_url = item.get_url();
		clicked_is_channel = item.type == YouTubeSuccinctItem::CHANNEL;
	});
	return res_view;
}
static std::pair<int *, std::string *> get_thumbnail_info_from_view(View *view) {
	SuccinctVideoView *cur_view0 = dynamic_cast<SuccinctVideoView *>(view);
	if (cur_view0) return {&cur_view0->thumbnail_handle, &cur_view0->thumbnail_url};
	SuccinctChannelView *cur_view1 = dynamic_cast<SuccinctChannelView *>(view);
	if (cur_view1) return {&cur_view1->thumbnail_handle, &cur_view1->thumbnail_url};
	usleep(1000000);
	return {NULL, NULL};
}
static void load_search_results(void *) {
	// pre-access processing
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	std::string search_word = cur_search_word;
	search_done = false;
	
	result_list_view->recursive_delete_subviews();
	set_loading_bottom_view();
	search_result = YouTubeSearchResult();
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
	
	// actual loading
	std::string search_url = "https://m.youtube.com/results?search_query=";
	for (auto c : search_word) {
		search_url.push_back('%');
		search_url.push_back("0123456789ABCDEF"[(u8) c / 16]);
		search_url.push_back("0123456789ABCDEF"[(u8) c % 16]);
	}
	add_cpu_limit(25);
	YouTubeSearchResult new_result = youtube_parse_search(search_url);
	remove_cpu_limit(25);
	
	// wrap and truncate here
	Util_log_save("search", "truncate/view creation start");
	std::vector<View *> new_result_views;
	for (size_t i = 0; i < new_result.results.size(); i++) new_result_views.push_back(result_item_to_view(new_result.results[i]));
	Util_log_save("search", "truncate/view creation end");
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
	search_result = new_result;
	result_list_view->views = new_result_views;
	update_result_bottom_view();
	
	search_done = true;
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static void load_more_search_results(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto prev_result = search_result;
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
	
	auto new_result = youtube_continue_search(prev_result);
	
	Util_log_save("search-c", "truncate/view creation start");
	std::vector<View *> new_result_views;
	for (size_t i = prev_result.results.size(); i < new_result.results.size(); i++) new_result_views.push_back(result_item_to_view(new_result.results[i]));
	Util_log_save("search-c", "truncate/view creation end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
	if (new_result.error != "") search_result.error = new_result.error;
	else {
		search_result = new_result;
		result_list_view->views.insert(result_list_view->views.end(), new_result_views.begin(), new_result_views.end());
	}
	update_result_bottom_view();
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static void access_input_url(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto url = last_url_input;
	svcReleaseMutex(resource_lock);
	
	YouTubePageType page_type = youtube_get_page_type(url);
	
	if (page_type == YouTubePageType::INVALID) {
		static NetworkSessionList session_list;
		if (!session_list.inited) session_list.init();
		
		auto result = Access_http_get(session_list, url, {});
		page_type = youtube_get_page_type(result.redirected_url);
		url = result.redirected_url;
		result.finalize();
		session_list.close_sessions();
	}
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
	if (page_type != YouTubePageType::INVALID) {
		url_jump_request_type = page_type;
		url_jump_request = url;
	} else {
		toast_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(NOT_A_YOUTUBE_URL); });
		toast_view->set_is_visible(true);
		toast_view_visible_frames_left = 100;
	}
	svcReleaseMutex(resource_lock);
}



static void search() {
	if (!is_async_task_running(load_search_results)) {
		SwkbdState keyboard;
		swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 32);
		swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY | SWKBD_PREDICTIVE_INPUT);
		swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
		swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, LOCALIZED(CANCEL).c_str(), false);
		swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, LOCALIZED(OK).c_str(), true);
		swkbdSetInitialText(&keyboard, cur_search_word.c_str());
		char search_word[129];
		add_cpu_limit(40);
		video_set_skip_drawing(true);
		auto button_pressed = swkbdInputText(&keyboard, search_word, 64);
		video_set_skip_drawing(false);
		remove_cpu_limit(40);
		
		if (button_pressed == SWKBD_BUTTON_RIGHT) {
			svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
			cur_search_word = search_word;
			search_box_view->set_text(search_word);
			search_box_view->set_get_text_color([] () { return DEFAULT_TEXT_COLOR; });
			svcReleaseMutex(resource_lock);
			
			remove_all_async_tasks_with_type(load_search_results);
			remove_all_async_tasks_with_type(load_more_search_results);
			queue_async_task(load_search_results, NULL);
		}
	}
}
static void url_input() {
	if (!is_async_task_running(access_input_url)) {
		SwkbdState keyboard;
		swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 255);
		swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY | SWKBD_PREDICTIVE_INPUT);
		swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
		swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, LOCALIZED(CANCEL).c_str(), false);
		swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, LOCALIZED(OK).c_str(), true);
		swkbdSetInitialText(&keyboard, last_url_input.c_str());
		char url[256];
		add_cpu_limit(40);
		video_set_skip_drawing(true);
		auto button_pressed = swkbdInputText(&keyboard, url, 256 - 1);
		video_set_skip_drawing(false);
		remove_cpu_limit(40);
		
		if (button_pressed == SWKBD_BUTTON_RIGHT) {
			svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
			last_url_input = url;
			svcReleaseMutex(resource_lock);
			
			remove_all_async_tasks_with_type(access_input_url);
			queue_async_task(access_input_url, NULL);
		}
	}
}

Intent Search_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::SEARCH);
	
	bool video_playing_bar_show = video_is_playing();
	RESULT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	result_view->update_y_range(0, RESULT_Y_HIGH - RESULT_Y_LOW);
	
	if (toast_view_visible_frames_left > 0) {
		toast_view_visible_frames_left--;
		if (!toast_view_visible_frames_left) toast_view->set_is_visible(false);
	}
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		if (var_debug_mode) Draw_debug_info();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		// (!) : I don't know how to draw textures truncated, so I will just fill the margin with white again
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		result_view->draw(0, RESULT_Y_LOW);
		Draw_texture(var_square_image[0], DEFAULT_BACK_COLOR, 0, 0, 320, RESULT_Y_LOW);
		top_bar_view->draw();
		if (toast_view_visible_frames_left > 0) toast_view->draw();
		svcReleaseMutex(resource_lock);
		
		// In night mode, the line between the search box and search results looks weird, so don't draw it
		if (!var_night_mode) Draw_texture(var_square_image[0], DEFAULT_TEXT_COLOR, 0, RESULT_Y_LOW - 1, 320, 1);
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT : 240 - OVERLAY_MENU_ICON_SIZE);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key, &intent, SceneType::SEARCH);
		
		top_bar_view->update(key);
		result_view->update(key, 0, RESULT_Y_LOW);
		if (clicked_url != "") {
			intent.next_scene = clicked_is_channel ? SceneType::CHANNEL : SceneType::VIDEO_PLAYER;
			intent.arg = clicked_url;
			clicked_url = "";
		}
		if (search_request) {
			search();
			search_request = false;
		}
		if (url_input_request) {
			url_input();
			url_input_request = false;
		}
		if (url_jump_request_type != YouTubePageType::INVALID) {
			if (url_jump_request_type == YouTubePageType::VIDEO) intent.next_scene = SceneType::VIDEO_PLAYER;
			else if (url_jump_request_type == YouTubePageType::CHANNEL) intent.next_scene = SceneType::CHANNEL;
			else if (url_jump_request_type == YouTubePageType::SEARCH) intent.next_scene = SceneType::SEARCH;
			intent.arg = url_jump_request;
			url_jump_request_type = YouTubePageType::INVALID;
		}
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		static int consecutive_scroll = 0;
		if (key.h_c_up || key.h_c_down) {
			if (key.h_c_up) consecutive_scroll = std::max(0, consecutive_scroll) + 1;
			else consecutive_scroll = std::min(0, consecutive_scroll) - 1;
			
			float scroll_amount = DPAD_SCROLL_SPEED0;
			if (std::abs(consecutive_scroll) > DPAD_SCROLL_SPEED1_THRESHOLD) scroll_amount = DPAD_SCROLL_SPEED1;
			if (key.h_c_up) scroll_amount *= -1;
			
			result_view->scroll(scroll_amount);
			var_need_reflesh = true;
		} else consecutive_scroll = 0;
		
		if (key.p_a) search();
		else if(key.h_touch || key.p_touch) var_need_reflesh = true;
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
	}
	
	svcReleaseMutex(resource_lock);
	
	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	
	return intent;
}
