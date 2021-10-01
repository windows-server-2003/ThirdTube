#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/watch_history.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui.hpp"
#include "ui/overlay.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/history.hpp"
#include "system/util/misc_tasks.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 30

namespace WatchHistory {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	std::vector<HistoryVideo> watch_history;
	std::string clicked_url;
	std::string erase_request;
	int thumbnail_request_l = 0;
	int thumbnail_request_r = 0;
	
	int cur_sort_type = 0;
	int sort_request = -1;
	
	int CONTENT_Y_HIGHT = 240; // changes according to whether the video playing bar is drawn or not
	
	OverlayView *on_long_tap_dialog;
	ScrollView *main_view = NULL;
	VerticalListView *video_list_view = NULL;
};
using namespace WatchHistory;

bool History_query_init_flag(void) {
	return already_init;
}

static void update_watch_history(const std::vector<HistoryVideo> &new_watch_history) {
	watch_history = new_watch_history;
	
	// clean up previous views and thumbnail requests
	if (video_list_view) for (auto view : video_list_view->views)
		thumbnail_cancel_request(dynamic_cast<SuccinctVideoView *>(view)->thumbnail_handle);
	thumbnail_request_l = thumbnail_request_r = 0;
	
	if (main_view) main_view->recursive_delete_subviews();
	delete main_view;
	
	// prepare new views
	video_list_view = new VerticalListView(0, 0, 320);
	for (auto i : watch_history) {
		std::string view_count_str;
		{
			std::string view_count_str_tmp = LOCALIZED(MY_VIEW_COUNT_WITH_NUMBER);
			for (size_t j = 0; j < view_count_str_tmp.size(); ) {
				if (j + 1 < view_count_str_tmp.size() && view_count_str_tmp[j] == '%' && view_count_str_tmp[j + 1] == '0')
					view_count_str += std::to_string(i.my_view_count), j += 2;
				else view_count_str.push_back(view_count_str_tmp[j]), j++;
			}
		}
		std::string last_watch_time_str;
		{
			char tmp[100];
			strftime(tmp, 100, "%Y/%m/%d %H:%M", gmtime(&i.last_watch_time));
			last_watch_time_str = tmp;
		}
		
		SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT + SMALL_MARGIN))
			->set_title_lines(i.title_lines)
			->set_auxiliary_lines({i.author_name, view_count_str + " " + last_watch_time_str})
			->set_bottom_right_overlay(i.length_text)
			->set_video_id(i.id);
		
		cur_view->set_get_background_color([] (const View &view) {
			int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
			if (var_night_mode) darkness = 0xFF - darkness;
			return COLOR_GRAY(darkness);
		})->set_on_view_released([i] (View &view) {
			clicked_url = youtube_get_video_url_by_id(i.id);
		})->add_on_long_hold(40, [i] (View &view) {
			on_long_tap_dialog->recursive_delete_subviews();
			on_long_tap_dialog
				->set_subview((new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
					->set_text((std::function<std::string ()>) [] () { return LOCALIZED(REMOVE_HISTORY_ITEM); })
					->set_x_centered(true)
					->set_y_centered(true)
					->set_text_offset(0, -1)
					->set_on_view_released([i] (View &view) {
						erase_request = i.id;
						main_view->reset_holding_status();
						on_long_tap_dialog->set_is_visible(false);
						var_need_reflesh = true;
					})
					->set_get_background_color([] (const View &view) {
						int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
						if (var_night_mode) darkness = 0xFF - darkness;
						return COLOR_GRAY(darkness);
					})
				)
				->set_on_cancel([] (const OverlayView &view) {
					main_view->reset_holding_status();
					on_long_tap_dialog->set_is_visible(false);
					var_need_reflesh = true;
				})
				->set_is_visible(true);
			var_need_reflesh = true;
		});
		
		
		video_list_view->views.push_back(cur_view);
	}
	constexpr int selector_width = 180;
	main_view = (new ScrollView(0, 0, 320, 240))
		->set_views({
			(new HorizontalListView(0, 0, MIDDLE_FONT_INTERVAL))
				->set_views({
					(new TextView(0, 0, 320 - selector_width, MIDDLE_FONT_INTERVAL))
						->set_text((std::function<std::string()>) [] () { return LOCALIZED(WATCH_HISTORY); })
						->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
					(new SelectorView(0, 0, selector_width, MIDDLE_FONT_INTERVAL))
						->set_texts({
							(std::function<std::string ()>) [] () { return LOCALIZED(BY_LAST_WATCH_TIME); },
							(std::function<std::string ()>) [] () { return LOCALIZED(BY_MY_VIEW_COUNT); }
						}, cur_sort_type)
						->set_on_change([](const SelectorView &view) { sort_request = cur_sort_type = view.selected_button; })
				}),
			(new HorizontalRuleView(0, 0, 320, 3)),
			video_list_view
		});
}


void History_resume(std::string arg)
{
	(void) arg;
	
	if (main_view) main_view->on_resume();
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
	
	update_watch_history(get_watch_history());
}

void History_suspend(void)
{
	thread_suspend = true;
}

void History_init(void)
{
	Util_log_save("history/init", "Initializing...");
	
	on_long_tap_dialog = new OverlayView(0, 0, 320, 240);
	on_long_tap_dialog->set_is_visible(false);
	
	History_resume("");
	already_init = true;
}

void History_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	Util_log_save("history/exit", "Exited.");
}

Intent History_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::HISTORY);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGHT = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_view->update_y_range(0, CONTENT_Y_HIGHT);
	
	
	// thumbnail request update
	int result_num = watch_history.size();
	if (result_num) {
		int displayed_l = std::min(result_num, main_view->get_offset() / VIDEO_LIST_THUMBNAIL_HEIGHT);
		int displayed_r = std::min(result_num, (main_view->get_offset() + CONTENT_Y_HIGHT - 1) / VIDEO_LIST_THUMBNAIL_HEIGHT + 1);
		int request_target_l = std::max(0, displayed_l - (MAX_THUMBNAIL_LOAD_REQUEST - (displayed_r - displayed_l)) / 2);
		int request_target_r = std::min(result_num, request_target_l + MAX_THUMBNAIL_LOAD_REQUEST);
		// transition from [thumbnail_request_l, thumbnail_request_r) to [request_target_l, request_target_r)
		std::set<int> new_indexes, cancelling_indexes;
		for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) cancelling_indexes.insert(i);
		for (int i = request_target_l; i < request_target_r; i++) new_indexes.insert(i);
		for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) new_indexes.erase(i);
		for (int i = request_target_l; i < request_target_r; i++) cancelling_indexes.erase(i);
		
		for (auto i : cancelling_indexes) {
			auto *cur_view = dynamic_cast<SuccinctVideoView *>(video_list_view->views[i]);
			thumbnail_cancel_request(cur_view->thumbnail_handle);
			cur_view->thumbnail_handle = -1;
		}
		for (auto i : new_indexes) {
			auto *cur_view = dynamic_cast<SuccinctVideoView *>(video_list_view->views[i]);
			cur_view->thumbnail_handle = thumbnail_request(youtube_get_video_thumbnail_url_by_id(cur_view->video_id),
				SceneType::HISTORY, 0, ThumbnailType::VIDEO_THUMBNAIL);
		}
		
		thumbnail_request_l = request_target_l;
		thumbnail_request_r = request_target_r;
		
		std::vector<std::pair<int, int> > priority_list(request_target_r - request_target_l);
		auto dist = [&] (int i) { return i < displayed_l ? displayed_l - i : i - displayed_r + 1; };
		for (int i = request_target_l; i < request_target_r; i++) priority_list[i - request_target_l] =
			{dynamic_cast<SuccinctVideoView *>(video_list_view->views[i])->thumbnail_handle, 500 - dist(i)};
		thumbnail_set_priorities(priority_list);
	}

	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		main_view->draw();
		on_long_tap_dialog->draw();
		
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
		if (on_long_tap_dialog->is_visible) on_long_tap_dialog->update(key);
		else {
			update_overlay_menu(&key, &intent, SceneType::HISTORY);
			
			main_view->update(key);
			if (clicked_url != "") {
				intent.next_scene = SceneType::VIDEO_PLAYER;
				intent.arg = clicked_url;
				clicked_url = "";
			}
			if (sort_request != -1) {
				auto tmp_watch_history = watch_history;
				std::sort(tmp_watch_history.begin(), tmp_watch_history.end(), [] (const HistoryVideo &i, const HistoryVideo &j) {
					if (sort_request == 0) return i.last_watch_time > j.last_watch_time;
					if (sort_request == 1) return i.my_view_count > j.my_view_count;
					// should not reach here
					return false;
				});
				update_watch_history(tmp_watch_history);
				
				sort_request = -1;
			}
			if (erase_request != "") {
				// erase 
				std::vector<HistoryVideo> tmp_watch_history;
				for (auto video : watch_history) if (video.id != erase_request) tmp_watch_history.push_back(video);
				update_watch_history(tmp_watch_history);
				history_erase_by_id(erase_request);
				misc_tasks_request(TASK_SAVE_HISTORY);
				
				erase_request = "";
			}
			if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		}
		if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	
	return intent;
}
