#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/channel.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "ui/ui.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/async_task.hpp"
#include "system/util/misc_tasks.hpp"
#include "system/util/subscription_util.hpp"

#define VIDEOS_MARGIN 6
#define VIDEOS_VERTICAL_INTERVAL (VIDEO_LIST_THUMBNAIL_HEIGHT + VIDEOS_MARGIN)
#define LOAD_MORE_MARGIN 30
#define BANNER_HEIGHT 55
#define ICON_SIZE 55
#define TAB_SELECTOR_HEIGHT 20
#define TAB_SELECTOR_SELECTED_LINE_HEIGHT 3
#define SUBSCRIBE_BUTTON_WIDTH 90
#define SUBSCRIBE_BUTTON_HEIGHT 25

#define MAX_THUMBNAIL_LOAD_REQUEST 30

#define TAB_NUM 2

namespace Channel {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	int VIDEO_LIST_Y_HIGH = 240;
	
	ScrollView *main_view = new ScrollView(0, 0, 320, VIDEO_LIST_Y_HIGH);
		ImageView *banner_view;
		ChannelView *channel_view;
		Tab2View *tab_view;
			// anonymous VerticalListView
				VerticalListView *video_list_view;
				TextView *video_load_more_view;
			// anonymous VerticalListView
				VerticalListView *playlist_list_view;
				TextView *playlist_load_more_view;
			VerticalListView *info_view;
	
	TextView *load_more_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))->set_x_centered(true);
	
	std::string clicked_url;
	
	Handle resource_lock;
	std::string cur_channel_url;
	YouTubeChannelDetail channel_info;
	std::map<std::string, YouTubeChannelDetail> channel_info_cache;
};
using namespace Channel;

static bool send_load_request(std::string url);
static void load_channel(void *);
static void load_channel_more(void *);
static void load_channel_playlists(void *);

void Channel_init(void)
{
	Util_log_save("channel/init", "Initializing...");
	Result_with_string result;
	
	svcCreateMutex(&resource_lock, false);
	
	
	banner_view = new ImageView(0, 0, 320, BANNER_HEIGHT);
	channel_view = new ChannelView(0, 0, 320, CHANNEL_ICON_SIZE + SMALL_MARGIN * 2);
	video_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST);
	video_load_more_view = (new TextView(0, 0, 320, 0));
	video_load_more_view
		->set_text((std::function<std::string ()>) [] () { return
			channel_info.error != "" ? channel_info.error : channel_info.has_continue() ? LOCALIZED(LOADING) : "";
		})
		->set_x_centered(true)
		->set_on_drawn([] (const View &) {
			if (channel_info.has_continue() && channel_info.error == "") {
				if (!is_async_task_running(load_channel) &&
					!is_async_task_running(load_channel_more)) queue_async_task(load_channel_more, NULL);
			}
		});
	playlist_list_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST);
	playlist_load_more_view = (new TextView(0, 0, 320, 0))
		->set_x_centered(true);
	info_view = (new VerticalListView(0, 0, 320));
	tab_view = (new Tab2View(0, 0, 320))->set_tab_texts({
		(std::function<std::string ()>) [] () { return LOCALIZED(VIDEOS); },
		(std::function<std::string ()>) [] () { return LOCALIZED(PLAYLISTS); },
		(std::function<std::string ()>) [] () { return LOCALIZED(INFO); }
	})->set_views({
		(new VerticalListView(0, 0, 320))->set_views({video_list_view, video_load_more_view}),
		(new VerticalListView(0, 0, 320))->set_views({playlist_list_view, playlist_load_more_view}),
		info_view
	});
	
	Channel_resume("");
	already_init = true;
}

void Channel_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	delete main_view;
	main_view = NULL;
	delete banner_view;
	banner_view = NULL;
	delete channel_view;
	channel_view = NULL;
	tab_view->recursive_delete_subviews();
	delete tab_view;
	tab_view = NULL;
	video_list_view = NULL;
	playlist_list_view = NULL;
	info_view = NULL;
	delete load_more_view;
	load_more_view = NULL;
	
	Util_log_save("search/exit", "Exited.");
}

void Channel_resume(std::string arg)
{
	if (arg != "" && arg != cur_channel_url) send_load_request(arg);
	overlay_menu_on_resume();
	main_view->on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}

void Channel_suspend(void) { thread_suspend = true; }


View *video2view(const YouTubeVideoSuccinct &video) {
	return (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT))
		->set_title_lines(truncate_str(video.title, 320 - (VIDEO_LIST_THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5))
		->set_thumbnail_url(video.thumbnail_url)
		->set_auxiliary_lines({video.publish_date, video.views_str})
		->set_bottom_right_overlay(video.duration_text)
		->set_get_background_color(View::STANDARD_BACKGROUND)
		->set_on_view_released([video] (View &) { clicked_url = video.url; });
}
View *playlist2view(const YouTubePlaylistSuccinct &playlist) {
	return (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT))
		->set_title_lines(truncate_str(playlist.title, 320 - (VIDEO_LIST_THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5))
		->set_thumbnail_url(playlist.thumbnail_url)
		->set_auxiliary_lines({playlist.video_count_str})
		->set_is_playlist(true)
		->set_get_background_color(View::STANDARD_BACKGROUND)
		->set_on_view_released([playlist] (View &) { clicked_url = playlist.url; });
}

static void load_channel(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto url = cur_channel_url;
	YouTubeChannelDetail result;
	bool need_loading = false;
	if (channel_info_cache.count(url)) result = channel_info_cache[url];
	else need_loading = true;
	main_view->set_views({load_more_view});
	load_more_view->set_text((std::function<std::string ()>) [] () { return LOCALIZED(LOADING); });
	svcReleaseMutex(resource_lock);
	
	if (need_loading) {
		add_cpu_limit(25);
		result = youtube_parse_channel_page(url);
		remove_cpu_limit(25);
	}
	
	// wrap and truncate here
	Util_log_save("channel", "truncate start");
	std::vector<View *> video_views;
	for (auto video : result.videos) video_views.push_back(video2view(video));
	std::vector<std::string> description_lines;
	{
		std::string cur_str;
		result.description.push_back('\n');
		for (auto c : result.description) {
			if (c == '\r') continue;
			if (c == '\n') {
				auto tmp = truncate_str(cur_str, 320 - SMALL_MARGIN * 2, 500, 0.5, 0.5);
				description_lines.insert(description_lines.end(), tmp.begin(), tmp.end());
				cur_str = "";
			} else cur_str.push_back(c);
		}
	}
	Util_log_save("channel", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	channel_info = result;
	channel_info_cache[url] = result;
	
	// banner
	thumbnail_cancel_request(banner_view->handle);
	banner_view->handle = -1;
	if (channel_info.banner_url != "") {
		banner_view->handle = thumbnail_request(channel_info.banner_url, SceneType::CHANNEL, 1000, ThumbnailType::VIDEO_BANNER);
		banner_view->update_y_range(0, BANNER_HEIGHT);
	} else banner_view->update_y_range(0, 0);
	// main channel view
	thumbnail_cancel_request(channel_view->icon_handle);
	channel_view
		->set_name(channel_info.name)
		->set_on_subscribe_button_released([] (const ChannelView &view) {
			bool cur_subscribed = subscription_is_subscribed(channel_info.id);
			if (cur_subscribed) subscription_unsubscribe(channel_info.id);
			else {
				SubscriptionChannel new_channel;
				new_channel.id = channel_info.id;
				new_channel.url = channel_info.url;
				new_channel.name = channel_info.name;
				new_channel.icon_url = channel_info.icon_url;
				new_channel.subscriber_count_str = channel_info.subscriber_count_str;
				subscription_subscribe(new_channel);
			}
			misc_tasks_request(TASK_SAVE_SUBSCRIPTION);
			var_need_reflesh = true;
		})
		->set_get_is_subscribed([] () { return subscription_is_subscribed(channel_info.id); })
		->set_icon_handle(thumbnail_request(channel_info.icon_url, SceneType::CHANNEL, 1001, ThumbnailType::ICON));
	// video list
	video_list_view->recursive_delete_subviews();
	video_list_view->set_views(video_views);
	if (result.error != "" || result.has_continue()) {
		video_load_more_view->update_y_range(0, DEFAULT_FONT_INTERVAL * 2);
		video_load_more_view->set_is_visible(true);
	} else {
		video_load_more_view->update_y_range(0, 0);
		video_load_more_view->set_is_visible(false);
	}
	// playlist list
	playlist_list_view->recursive_delete_subviews();
	playlist_load_more_view
		->set_text((std::function<std::string ()>) [] () { return channel_info.error != "" ? channel_info.error : LOCALIZED(LOADING); })
		->update_y_range(0, DEFAULT_FONT_INTERVAL * 2)
		->set_on_drawn([] (const View &) {
			if (channel_info.error == "") {
				if (!is_async_task_running(load_channel) &&
					!is_async_task_running(load_channel_playlists)) queue_async_task(load_channel_playlists, NULL);
			}
		});
	// video info
	info_view->recursive_delete_subviews();
	info_view->set_views({
		(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text((std::function<std::string ()>) [] () { return LOCALIZED(CHANNEL_DESCRIPTION); })
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
		(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN)),
		(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * description_lines.size()))
			->set_text_lines(description_lines),
		(new EmptyView(0, 0, 320, SMALL_MARGIN * 2))
	});
	
	
	main_view->set_views({banner_view, channel_view, tab_view}); 
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static void load_channel_more(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto prev_result = channel_info;
	svcReleaseMutex(resource_lock);
	
	auto new_result = youtube_channel_page_continue(prev_result);
	
	Util_log_save("channel-c", "truncate start");
	std::vector<View *> new_video_views;
	for (size_t i = prev_result.videos.size(); i < new_result.videos.size(); i++)
		new_video_views.push_back(video2view(new_result.videos[i]));
	Util_log_save("channel-c", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") channel_info.error = new_result.error;
	else {
		channel_info = new_result;
		channel_info_cache[channel_info.url_original] = channel_info;
	}
	video_list_view->views.insert(video_list_view->views.end(), new_video_views.begin(), new_video_views.end());
	if (channel_info.error != "" || channel_info.has_continue()) {
		video_load_more_view->update_y_range(0, DEFAULT_FONT_INTERVAL);
		video_load_more_view->set_is_visible(true);
	} else {
		video_load_more_view->update_y_range(0, 0);
		video_load_more_view->set_is_visible(false);
	}
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static void load_channel_playlists(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto prev_result = channel_info;
	svcReleaseMutex(resource_lock);
	
	auto new_result = youtube_channel_load_playlists(prev_result);
	
	Util_log_save("channel-p", "truncate start");
	std::vector<View *> new_playlist_views;
	for (size_t i = prev_result.playlists.size(); i < new_result.playlists.size(); i++)
		new_playlist_views.push_back(playlist2view(new_result.playlists[i]));
	Util_log_save("channel-p", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") channel_info.error = new_result.error;
	else {
		channel_info = new_result;
		channel_info_cache[channel_info.url_original] = channel_info;
	}
	playlist_list_view->views.insert(playlist_list_view->views.end(), new_playlist_views.begin(), new_playlist_views.end());
	// channel playlist doesn't seem to have continuation
	playlist_load_more_view
		->set_text("")
		->update_y_range(0, 0)
		->set_on_drawn(std::function<void (View &view)> ());
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static bool send_load_request(std::string url) {
	if (!is_async_task_running(load_channel)) {
		remove_all_async_tasks_with_type(load_channel_more);
		
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		cur_channel_url = url;
		svcReleaseMutex(resource_lock);
		
		queue_async_task(load_channel, NULL);
		return true;
	} else return false;
}

Intent Channel_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::CHANNEL);
	
	bool video_playing_bar_show = video_is_playing();
	VIDEO_LIST_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_view->update_y_range(0, VIDEO_LIST_Y_HIGH);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		main_view->draw();
		svcReleaseMutex(resource_lock);
		
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
		update_overlay_menu(&key, &intent, SceneType::CHANNEL);
		
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		main_view->update(key);
		svcReleaseMutex(resource_lock);
		
		if (clicked_url != "") {
			intent.next_scene = SceneType::VIDEO_PLAYER;
			intent.arg = clicked_url;
			clicked_url = "";
		}
		
		static int consecutive_scroll = 0;
		if (key.h_c_up || key.h_c_down) {
			if (key.h_c_up) consecutive_scroll = std::max(0, consecutive_scroll) + 1;
			else consecutive_scroll = std::min(0, consecutive_scroll) - 1;
			
			float scroll_amount = DPAD_SCROLL_SPEED0;
			if (std::abs(consecutive_scroll) > DPAD_SCROLL_SPEED1_THRESHOLD) scroll_amount = DPAD_SCROLL_SPEED1;
			if (key.h_c_up) scroll_amount *= -1;
			
			main_view->scroll(scroll_amount);
			var_need_reflesh = true;
		} else consecutive_scroll = 0;
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		
		if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
