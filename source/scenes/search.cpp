#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/search.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "ui/ui.hpp"
#include "network/thumbnail_loader.hpp"
#include "network/network_io.hpp"
#include "system/util/async_task.hpp"

#define SEARCH_BOX_MARGIN 4

#define URL_BUTTON_WIDTH 60

#define MAX_THUMBNAIL_LOAD_REQUEST 12


namespace Search {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	Mutex resource_lock;
	std::string cur_search_word = "";
	YouTubeSearchResult search_result;
	bool search_done = false;
	
	std::string last_url_input = "https://m.youtube.com/watch?v=";
	
	bool clicked_is_channel;
	std::string clicked_url;
	
	TextView * toast_view = (new TextView((320 - 150) / 2, 190, 150, DEFAULT_FONT_INTERVAL + SMALL_MARGIN));
	int toast_view_visible_frames_left = 0;
	
	const int RESULT_Y_LOW = 25;
	int RESULT_Y_HIGH = 240; // changes according to whether the video playing bar is drawn or not
	
	VerticalListView *main_view;
		VerticalListView *top_bar_view = new VerticalListView(0, 0, 320);
			// annonymous HorizontalListView
				TextView *search_box_view;
				TextView *url_button_view;
			// annonymous RuleView : height 1
		ScrollView *result_view;
			VerticalListView *result_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)
			->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::SEARCH);
			View *result_bottom_view = new EmptyView(0, 0, 320, 0);
};
using namespace Search;

TextView *Search_get_toast_view() { return toast_view; }
View *Search_get_search_bar_view() { return top_bar_view; }

static void load_search_results(void *);
static void load_more_search_results(void *);

void Search_init(void) {
	Util_log_save("search/init", "Initializing...");
	Result_with_string result;
	
	search_box_view = (new TextView(0, SEARCH_BOX_MARGIN, 320 - SEARCH_BOX_MARGIN * 3 - URL_BUTTON_WIDTH, RESULT_Y_LOW - SEARCH_BOX_MARGIN * 2 - 1));
	search_box_view->set_text_offset(0, -1);
	search_box_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SEARCH_HINT); });
	search_box_view->set_get_background_color([] (const View &) { return LIGHT0_BACK_COLOR; });
	search_box_view->set_get_text_color([] () { return LIGHT1_TEXT_COLOR; });
	search_box_view->set_on_view_released([] (View &) {
		if (Search_show_search_keyboard()) global_intent.next_scene = SceneType::SEARCH;
	});
	
	url_button_view = (new TextView(0, SEARCH_BOX_MARGIN, URL_BUTTON_WIDTH, RESULT_Y_LOW - SEARCH_BOX_MARGIN * 2 - 1));
	url_button_view->set_text_offset(0, -1);
	url_button_view->set_x_alignment(TextView::XAlign::CENTER);
	url_button_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(URL); });
	url_button_view->set_get_background_color([] (const View &) { return LIGHT1_BACK_COLOR; });
	url_button_view->set_on_view_released([] (View &) {
		if (Search_show_url_input_keyboard()) global_intent.next_scene = SceneType::SEARCH;
	});
	
	toast_view->set_is_visible(false);
	toast_view->set_x_alignment(TextView::XAlign::CENTER);
	toast_view->set_text_offset(0, -1);
	toast_view->set_get_background_color([] (const View &) { return 0x50000000; });
	toast_view->set_get_text_color([] () { return (u32) -1; });
	
	top_bar_view
		->set_views({
			(new HorizontalListView(0, 0, RESULT_Y_LOW - 1))
				->set_views({
					new EmptyView(0, 0, SEARCH_BOX_MARGIN, RESULT_Y_LOW - 1),
					search_box_view,
					new EmptyView(0, 0, SEARCH_BOX_MARGIN, RESULT_Y_LOW - 1),
					url_button_view,
					new EmptyView(0, 0, SEARCH_BOX_MARGIN, RESULT_Y_LOW - 1)
				})
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			(new RuleView(0, 0, 320, 1))
				->set_get_color([] () { return 0xFF000000; }) // always black
				->set_margin(0)
		});
		
	
	result_view = (new ScrollView(0, 0, 320, RESULT_Y_HIGH - RESULT_Y_LOW))
		->set_views({result_list_view, result_bottom_view});
	main_view = (new VerticalListView(0, 0, 320))
		->set_views({
			top_bar_view,
			result_view
		})
		->set_draw_order({1, 0});
	
	
	Search_resume("");
	already_init = true;
}

void Search_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	resource_lock.lock();
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	result_view = NULL;
	result_list_view = NULL;
	result_bottom_view = NULL;
	top_bar_view = NULL;
	search_box_view = NULL;
	url_button_view = NULL;
	
	toast_view->recursive_delete_subviews();
	delete toast_view;
	toast_view = NULL;
	
	resource_lock.unlock();
	
	
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
		->set_x_alignment(TextView::XAlign::CENTER);
	result_view->views[1] = result_bottom_view;
}
static void update_result_bottom_view() {
	delete result_bottom_view;
	if (search_result.error != "" || search_result.has_continue()) {
		TextView *cur_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))->set_x_alignment(TextView::XAlign::CENTER);
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
	resource_lock.lock();
	std::string search_word = cur_search_word;
	search_done = false;
	
	result_list_view->recursive_delete_subviews();
	set_loading_bottom_view();
	search_result = YouTubeSearchResult();
	var_need_reflesh = true;
	resource_lock.unlock();
	
	// actual loading
	std::string search_url = "https://m.youtube.com/results?search_query=";
	for (auto c : search_word) {
		search_url.push_back('%');
		search_url.push_back("0123456789ABCDEF"[(u8) c / 16]);
		search_url.push_back("0123456789ABCDEF"[(u8) c % 16]);
	}
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	YouTubeSearchResult new_result = youtube_parse_search(search_url);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	// wrap and truncate here
	Util_log_save("search", "truncate/view creation start");
	std::vector<View *> new_result_views;
	for (size_t i = 0; i < new_result.results.size(); i++) new_result_views.push_back(result_item_to_view(new_result.results[i]));
	Util_log_save("search", "truncate/view creation end");
	
	resource_lock.lock();
	if (exiting) { // app shut down while loading
		resource_lock.unlock();
		return;
	}
	search_result = new_result;
	result_list_view->views = new_result_views;
	update_result_bottom_view();
	
	search_done = true;
	var_need_reflesh = true;
	resource_lock.unlock();
}
static void load_more_search_results(void *) {
	resource_lock.lock();
	auto prev_result = search_result;
	var_need_reflesh = true;
	resource_lock.unlock();
	
	auto new_result = youtube_continue_search(prev_result);
	
	Util_log_save("search-c", "truncate/view creation start");
	std::vector<View *> new_result_views;
	for (size_t i = prev_result.results.size(); i < new_result.results.size(); i++) new_result_views.push_back(result_item_to_view(new_result.results[i]));
	Util_log_save("search-c", "truncate/view creation end");
	
	
	resource_lock.lock();
	if (exiting) { // app shut down while loading
		resource_lock.unlock();
		return;
	}
	if (new_result.error != "") search_result.error = new_result.error;
	else {
		search_result = new_result;
		result_list_view->views.insert(result_list_view->views.end(), new_result_views.begin(), new_result_views.end());
	}
	update_result_bottom_view();
	var_need_reflesh = true;
	resource_lock.unlock();
}
static void access_input_url(void *) {
	resource_lock.lock();
	auto url = last_url_input;
	resource_lock.unlock();
	
	YouTubePageType page_type = youtube_get_page_type(url);
	
	if (page_type == YouTubePageType::INVALID) {
		static NetworkSessionList session_list;
		if (!session_list.inited) session_list.init();
		
		auto result = session_list.perform(HttpRequest::GET(url, {}));
		page_type = youtube_get_page_type(result.redirected_url);
		url = result.redirected_url;
	}
	
	resource_lock.lock();
	if (exiting) { // app shut down while loading
		resource_lock.unlock();
		return;
	}
	if (page_type != YouTubePageType::INVALID) {
		if (page_type == YouTubePageType::VIDEO) global_intent.next_scene = SceneType::VIDEO_PLAYER;
		else if (page_type == YouTubePageType::CHANNEL) global_intent.next_scene = SceneType::CHANNEL;
		else if (page_type == YouTubePageType::SEARCH) global_intent.next_scene = SceneType::SEARCH;
		global_intent.arg = url;
	} else {
		toast_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(NOT_A_YOUTUBE_URL); });
		toast_view->set_is_visible(true);
		toast_view_visible_frames_left = 100;
	}
	resource_lock.unlock();
}



bool Search_show_search_keyboard() {
	if (!is_async_task_running(load_search_results)) {
		if (global_current_scene != SceneType::SEARCH) resource_lock.lock();
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
			cur_search_word = search_word;
			search_box_view->set_text(search_word);
			search_box_view->set_get_text_color([] () { return DEFAULT_TEXT_COLOR; });
			
			remove_all_async_tasks_with_type(load_search_results);
			remove_all_async_tasks_with_type(load_more_search_results);
			queue_async_task(load_search_results, NULL);
		}
		if (global_current_scene != SceneType::SEARCH) resource_lock.unlock();
		return button_pressed == SWKBD_BUTTON_RIGHT;
	} else return false;
}
bool Search_show_url_input_keyboard() {
	if (!is_async_task_running(access_input_url)) {
		if (global_current_scene != SceneType::SEARCH) resource_lock.lock();
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
			last_url_input = url;
			
			remove_all_async_tasks_with_type(access_input_url);
			queue_async_task(access_input_url, NULL);
		}
		if (global_current_scene != SceneType::SEARCH) resource_lock.unlock();
		return button_pressed == SWKBD_BUTTON_RIGHT;
	} else return false;
}

void Search_draw(void)
{
	Hid_info key;
	Util_hid_query_key_state(&key);
	
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
		video_draw_top_screen();

		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		// (!) : I don't know how to draw textures truncated, so I will just fill the margin with white again
		resource_lock.lock();
		main_view->draw();
		if (toast_view_visible_frames_left > 0) toast_view->draw();
		resource_lock.unlock();
		
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
	

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key);
		
		resource_lock.lock();
		main_view->update(key);
		if (clicked_url != "") {
			global_intent.next_scene = clicked_is_channel ? SceneType::CHANNEL : SceneType::VIDEO_PLAYER;
			global_intent.arg = clicked_url;
			clicked_url = "";
		}
		resource_lock.unlock();
		
		if (video_playing_bar_show) video_update_playing_bar(key);
		
		if (key.p_a) Search_show_search_keyboard();
		
		if (key.p_b) global_intent.next_scene = SceneType::BACK;
	}
}
