#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/home.hpp"
#include "scenes/search.hpp"
#include "scenes/video_player.hpp"
#include "data_io/subscription_util.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui.hpp"
#include "ui/overlay.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "util/misc_tasks.hpp"
#include "util/async_task.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 12

#define FEED_RELOAD_BUTTON_HEIGHT 18
#define TOP_HEIGHT 25
#define VIDEO_TITLE_MAX_WIDTH (320 - SMALL_MARGIN * 2 - VIDEO_LIST_THUMBNAIL_WIDTH)

namespace Home {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	Mutex resource_lock;
	
	YouTubeHomeResult home_info;
	std::vector<SubscriptionChannel> subscribed_channels;
	bool clicked_is_video;
	std::string clicked_url;
	
	int feed_loading_progress = 0;
	int feed_loading_total = 0;
	
	int CONTENT_Y_HIGH = 240; // changes according to whether the video playing bar is drawn or not
	
	VerticalListView *main_view = NULL;
	TabView *main_tab_view = NULL;
		ScrollView *home_tab_view = NULL;
			VerticalListView *home_videos_list_view = NULL;
			View *home_videos_bottom_view = new EmptyView(0, 0, 320, 0);
		ScrollView *channels_tab_view = NULL;
			VerticalListView *channels_tab_list_view = NULL;
		VerticalListView *feed_tab_view = NULL;
			ScrollView *feed_videos_view = NULL;
			VerticalListView *feed_videos_list_view = NULL;
};
using namespace Home;

static void load_home_page(void *);
static void load_home_page_more(void *);
static void load_subscription_feed(void *);
static void update_subscribed_channels(const std::vector<SubscriptionChannel> &new_subscribed_channels);

void Home_init(void) {
	logger.info("subsc/init", "Initializing...");
	
	home_videos_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)
		->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	home_tab_view = (new ScrollView(0, 0, 320, 0))->set_views({home_videos_list_view, home_videos_bottom_view});
	channels_tab_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)
		->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	channels_tab_view = (new ScrollView(0, 0, 320, 0))->set_views({channels_tab_list_view}); // height : dummy(set properly by set_stretch_subview(true) on main_tab_view)
	feed_videos_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)
		->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	feed_videos_view = (new ScrollView(0, 0, 320, 0))->set_views({feed_videos_list_view});
	feed_tab_view = (new VerticalListView(0, 0, 320))
		->set_views({
			(new TextView(0, 0, 320, FEED_RELOAD_BUTTON_HEIGHT))
				->set_text((std::function<std::string ()>) [] () {
					auto res = LOCALIZED(RELOAD);
					if (is_async_task_running(load_subscription_feed)) res += " (" + std::to_string(feed_loading_progress) + "/" + std::to_string(feed_loading_total) + ")";
					return res;
				})
				->set_text_offset(SMALL_MARGIN, -1)
				->set_on_view_released([] (View &) {
					if (!is_async_task_running(load_subscription_feed))
						queue_async_task(load_subscription_feed, NULL);
				})
				->set_get_background_color([] (const View &view) -> u32 {
					if (is_async_task_running(load_subscription_feed)) return LIGHT0_BACK_COLOR;
					return View::STANDARD_BACKGROUND(view);
				}),
			(new RuleView(0, 0, 320, SMALL_MARGIN))->set_margin(0)->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			feed_videos_view
		})
		->set_draw_order({2, 1, 0});
	main_tab_view = (new TabView(0, 0, 320, CONTENT_Y_HIGH - TOP_HEIGHT))
		->set_views({home_tab_view, channels_tab_view, feed_tab_view})
		->set_tab_texts<std::function<std::string ()> >({
			[] () { return LOCALIZED(HOME); },
			[] () { return LOCALIZED(SUBSCRIBED_CHANNELS); },
			[] () { return LOCALIZED(NEW_VIDEOS); }
		});
	main_view = (new VerticalListView(0, 0, 320))
		->set_views({
			(new CustomView(0, 0, 320, 25))
				->set_draw([] (const CustomView&) {
					return Search_get_search_bar_view()->draw();
				})
				->set_update([] (const CustomView&, Hid_info key) {
					return Search_get_search_bar_view()->update(key);
				}),
			main_tab_view
		})
		->set_draw_order({1, 0});
	
	queue_async_task(load_home_page, NULL);
	load_subscription();
	
	Home_resume("");
	already_init = true;
}
void Home_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	resource_lock.lock();
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	feed_tab_view = NULL;
	feed_videos_view = NULL;
	feed_videos_list_view = NULL;
	channels_tab_view = NULL;
	channels_tab_list_view = NULL;
	
	resource_lock.unlock();
	
	logger.info("subsc/exit", "Exited.");
}
void Home_suspend(void) {
	thread_suspend = true;
}
void Home_resume(std::string arg) {
	(void) arg;
	
	// main_tab_view->on_resume();
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
	
	resource_lock.lock();
	update_subscribed_channels(get_subscribed_channels());
	resource_lock.unlock();
}


// async functions
static SuccinctVideoView *convert_video_to_view(const YouTubeVideoSuccinct &video) {
	SuccinctVideoView *res = new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT);
	res->set_title_lines(truncate_str(video.title, 320 - (VIDEO_LIST_THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5));
	res->set_auxiliary_lines({video.views_str, video.publish_date});
	res->set_bottom_right_overlay(video.duration_text);
	res->set_thumbnail_url(video.thumbnail_url);
	res->set_get_background_color(View::STANDARD_BACKGROUND);
	res->set_on_view_released([video] (View &view) {
		clicked_url = video.url;
		clicked_is_video = true;
	});
	return res;
}
static void update_home_bottom_view(bool force_show_loading) {
	delete home_videos_bottom_view;
	if (home_info.has_more_results() || force_show_loading) {
		home_videos_bottom_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
			->set_text([] () {
				if (home_info.error != "") return home_info.error;
				else return LOCALIZED(LOADING);
			})
			->set_x_alignment(TextView::XAlign::CENTER)
			->set_on_drawn([] (View &) {
				if (!is_async_task_running(load_home_page) && !is_async_task_running(load_home_page_more) && home_info.error == "")
					queue_async_task(load_home_page_more, NULL);
			});
	} else home_videos_bottom_view = new EmptyView(0, 0, 320, 0);
	home_tab_view->set_views({home_videos_list_view, home_videos_bottom_view});
}
static void load_home_page(void *) {
	resource_lock.lock();
	update_home_bottom_view(true);
	var_need_reflesh = true;
	resource_lock.unlock();
	
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto results = youtube_load_home_page();
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	logger.info("home", "truncate/view creation start");
	std::vector<View *> new_videos_view;
	for (auto video : results.videos) new_videos_view.push_back(convert_video_to_view(video));
	logger.info("home", "truncate/view creation end");
	
	resource_lock.lock();
	if (results.error == "") {
		home_info = results;
		home_videos_list_view->recursive_delete_subviews();
		home_videos_list_view->set_views(new_videos_view);
		update_home_bottom_view(false);
	} else home_info.error = results.error;
	var_need_reflesh = true;
	resource_lock.unlock();
}
static void load_home_page_more(void *) {
	auto new_result = home_info;
	new_result.load_more_results();
	
	logger.info("home-c", "truncate/view creation start");
	std::vector<View *> new_videos_view;
	for (size_t i = home_info.videos.size(); i < new_result.videos.size(); i++) new_videos_view.push_back(convert_video_to_view(new_result.videos[i]));
	logger.info("home-c", "truncate/view creation end");
	
	resource_lock.lock();
	home_info = new_result;
	if (new_result.error == "") {
		home_videos_list_view->views.insert(home_videos_list_view->views.end(), new_videos_view.begin(), new_videos_view.end());
		update_home_bottom_view(false);
	}
	resource_lock.unlock();
}
static void load_subscription_feed(void *) {
	resource_lock.lock();
	auto channels = subscribed_channels;
	resource_lock.unlock();
	
	std::vector<std::string> ids;
	for (auto channel : channels) ids.push_back(channel.id);
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto results = youtube_load_channel_page_multi(ids, [] (int cur, int total) {
		feed_loading_progress = cur;
		feed_loading_total = total;
		var_need_reflesh = true;
	});
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	std::map<std::pair<int, int>, std::vector<YouTubeVideoSuccinct> > loaded_videos;
	for (auto result : results) {
		// update the subscription metadata at the same time
		if (result.name != "") {
			SubscriptionChannel new_info;
			new_info.id = result.id;
			new_info.url = result.url;
			new_info.name = result.name;
			new_info.icon_url = result.icon_url;
			new_info.subscriber_count_str = result.subscriber_count_str;
			subscription_unsubscribe(result.id);
			subscription_subscribe(new_info);
		}
		
		int loaded_cnt = 0;
		for (auto video : result.videos) {
			std::string date_number_str;
			for (auto c : video.publish_date) if (isdigit(c)) date_number_str.push_back(c);
			
			// 1 : seconds
			char *end;
			int number = strtoll(date_number_str.c_str(), &end, 10);
			if (*end) {
				logger.error("subsc", "failed to parse the integer in date : " + video.publish_date);
				continue;
			}
			int unit = -1;
			std::vector<std::vector<std::string> > unit_list = {
				{"second", "秒"},
				{"minute", "分"},
				{"hour", "時間"},
				{"day", "日"},
				{"week", "週間"},
				{"month", "月"},
				{"year", "年"}
			};
			for (size_t i = 0; i < unit_list.size(); i++) {
				bool matched = false;
				for (auto pattern : unit_list[i]) if (video.publish_date.find(pattern) != std::string::npos) {
					matched = true;
					break;
				}
				if (matched) {
					unit = i;
					break;
				}
			}
			if (unit == -1) {
				logger.error("subsc", "failed to parse the unit of date : " + video.publish_date);
				continue;
			}
			if (std::pair<int, int>{unit, number} > std::pair<int, int>{5, 2}) continue; // more than 2 months old
			loaded_cnt++;
			loaded_videos[{unit, number}].push_back(video);
		}
		logger.info("subsc", "loaded " + result.name + " : " + std::to_string(loaded_cnt) + " video(s)");
	}
	
	std::vector<View *> new_feed_video_views;
	for (auto &i : loaded_videos) {
		for (auto video : i.second) {
			SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT));
			
			cur_view->set_title_lines(truncate_str(video.title, VIDEO_TITLE_MAX_WIDTH, 2, 0.5, 0.5));
			cur_view->set_thumbnail_url(video.thumbnail_url);
			cur_view->set_auxiliary_lines({video.publish_date, video.views_str});
			cur_view->set_bottom_right_overlay(video.duration_text);
			cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
			cur_view->set_on_view_released([video] (View &view) {
				clicked_url = video.url;
				clicked_is_video = true;
			});
			
			new_feed_video_views.push_back(cur_view);
		}
	}
	
	misc_tasks_request(TASK_SAVE_SUBSCRIPTION);
	
	resource_lock.lock();
	if (exiting) { // app shut down while loading
		resource_lock.unlock();
		return;
	}
	update_subscribed_channels(get_subscribed_channels());
	
	feed_videos_list_view->recursive_delete_subviews();
	feed_videos_list_view->views = new_feed_video_views;
	resource_lock.unlock();
}

static void update_subscribed_channels(const std::vector<SubscriptionChannel> &new_subscribed_channels) {
	subscribed_channels = new_subscribed_channels;
	
	// prepare new views
	std::vector<View *> new_views;
	for (auto channel : new_subscribed_channels) {
		SuccinctChannelView *cur_view = (new SuccinctChannelView(0, 0, 320, CHANNEL_ICON_HEIGHT));
		cur_view->set_name(channel.name);
		cur_view->set_auxiliary_lines({channel.subscriber_count_str});
		cur_view->set_thumbnail_url(channel.icon_url);
		cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
		cur_view->set_on_view_released([channel] (View &view) {
			clicked_url = channel.url;
			clicked_is_video = false;
		});
		new_views.push_back(cur_view);
	}
	channels_tab_list_view->swap_views(new_views); // avoid unnecessary thumbnail reloading
}




void Home_draw(void) {
	Hid_info key;
	Util_hid_query_key_state(&key);
	
	thumbnail_set_active_scene(SceneType::HOME);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT);
	
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		video_draw_top_screen();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		resource_lock.lock();
		main_view->draw();
		resource_lock.unlock();
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(CONTENT_Y_HIGH - OVERLAY_MENU_ICON_SIZE - main_tab_view->tab_selector_height);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	

	resource_lock.lock();

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key);
		
		home_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		channels_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		feed_videos_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height - FEED_RELOAD_BUTTON_HEIGHT - SMALL_MARGIN);
		main_view->update(key);
		if (clicked_url != "") {
			global_intent.next_scene = clicked_is_video ? SceneType::VIDEO_PLAYER : SceneType::CHANNEL;
			global_intent.arg = clicked_url;
			clicked_url = "";
		}
		if (video_playing_bar_show) video_update_playing_bar(key);
		
		if (key.p_b) global_intent.next_scene = SceneType::BACK;
	}
	resource_lock.unlock();
}
