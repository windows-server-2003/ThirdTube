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
#define COMMUNITY_POST_MAX_WIDTH (320 - (POST_ICON_SIZE + 2 * SMALL_MARGIN))

#define COMMUNITY_POST_MAX_LINES 100
#define MAX_THUMBNAIL_LOAD_REQUEST 20

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
			// playlists : Tab2View (if there are playlists loaded) or TextView (if they're not loaded or the channel has no playlist)
			// annonymous VerticalListView
				// an EmptyView for margin
				VerticalListView *community_post_list_view;
				TextView *community_post_load_more_view;
			VerticalListView *info_view;
	
	TextView *load_more_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))->set_x_alignment(TextView::XAlign::CENTER);
	float community_post_y = 0; // y coordinate of the upper bound of community posts list, updated every frames, used for thumbnail request updating
	
	std::string clicked_url;
	
	std::set<PostView *> community_thumbnail_loaded_list;
	
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
static void load_channel_community_posts(void *);

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
		->set_x_alignment(TextView::XAlign::CENTER)
		->set_on_drawn([] (const View &) {
			if (channel_info.has_continue() && channel_info.error == "") {
				if (!is_async_task_running(load_channel) &&
					!is_async_task_running(load_channel_more)) queue_async_task(load_channel_more, NULL);
			}
		});
	community_post_list_view = (new VerticalListView(0, 0, 320));
	community_post_list_view->set_on_drawn([] (View &view) { community_post_y = view.y0; });
	community_post_load_more_view = (new TextView(0, 0, 320, 0));
	community_post_load_more_view
		->set_text((std::function<std::string ()>) [] () { 
			return channel_info.error == "" && channel_info.has_community_posts_to_load() ? LOCALIZED(LOADING) : "";
		})
		->set_x_alignment(TextView::XAlign::CENTER)
		->set_on_drawn([] (View &) {
			if (channel_info.error == "" && channel_info.has_community_posts_to_load()) {
				if (!is_async_task_running(load_channel) &&
					!is_async_task_running(load_channel_community_posts)) queue_async_task(load_channel_community_posts, NULL);
			}
		});
	info_view = (new VerticalListView(0, 0, 320));
	tab_view = (new Tab2View(0, 0, 320))->set_tab_texts<std::function<std::string ()> >({
		[] () { return LOCALIZED(VIDEOS); },
		[] () { return LOCALIZED(PLAYLISTS); },
		[] () { return LOCALIZED(COMMUNITY); },
		[] () { return LOCALIZED(INFO); }
	})->set_views({
		(new VerticalListView(0, 0, 320))->set_views({video_list_view, video_load_more_view}),
		(new EmptyView(0, 0, 320, 0)),
		(new VerticalListView(0, 0, 320))->set_views({
			(new EmptyView(0, 0, 320, SMALL_MARGIN)),
			community_post_list_view,
			community_post_load_more_view
		}),
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
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	
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
	info_view = NULL;
	delete load_more_view;
	load_more_view = NULL;
	
	svcReleaseMutex(resource_lock);
	
	Util_log_save("search/exit", "Exited.");
}

void Channel_resume(std::string arg)
{
	if (arg != "" && arg != cur_channel_url) {
		send_load_request(arg);
		tab_view->selected_tab = 0;
	}
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
View *get_playlist_categories_tab_view(const std::vector<std::pair<std::string, std::vector<YouTubePlaylistSuccinct> > > &playlist_categories) {
	if (playlist_categories.size()) {
		Tab2View *res_view = new Tab2View(0, 0, 320);
		
		std::vector<std::string> titles;
		for (auto playlist_category : playlist_categories) {
			titles.push_back(playlist_category.first);
			
			VerticalListView *cur_list_view = (new VerticalListView(0, 0, 320))
				->set_margin(SMALL_MARGIN)
				->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST);
			for (auto playlist : playlist_category.second) cur_list_view->views.push_back(playlist2view(playlist));
			res_view->views.push_back(cur_list_view);
		}
		res_view->set_tab_texts<std::string>(titles);
		return res_view;
	} else return (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
		->set_text((std::function<std::string ()>) [] () { return LOCALIZED(EMPTY); })
		->set_x_alignment(TextView::XAlign::CENTER);
}
View *community_post_2_view(const YouTubeChannelDetail::CommunityPost &post) {
	PostView *res = new PostView(0, 0, 320);
	
	auto &cur_content = post.message;
	std::vector<std::string> content_lines;
	auto itr = cur_content.begin();
	while (itr != cur_content.end()) {
		if (content_lines.size() >= COMMUNITY_POST_MAX_LINES) break;
		auto next_itr = std::find(itr, cur_content.end(), '\n');
		auto tmp = truncate_str(std::string(itr, next_itr), COMMUNITY_POST_MAX_WIDTH, COMMUNITY_POST_MAX_LINES - content_lines.size(), 0.5, 0.5);
		content_lines.insert(content_lines.end(), tmp.begin(), tmp.end());
		
		if (next_itr != cur_content.end()) itr = std::next(next_itr);
		else break;
	}
	if (post.poll_choices.size()) {
		content_lines.push_back("");
		for (auto choice : post.poll_choices) content_lines.push_back(" - " + choice);
	}
	
	res
		->set_author_name(post.author_name)
		->set_author_icon_url(post.author_icon_url)
		->set_time_str(post.time)
		->set_upvote_str(post.upvotes_str)
		->set_additional_image_url(post.image_url)
		->set_content_lines(content_lines)
		->set_has_more_replies([] () { return false; });
	
	if (post.poll_choices.size()) res->lines_shown = content_lines.size();
	if (post.video.title != "") {
		std::string video_url = post.video.url;
		res->additional_video_view = (new SuccinctVideoView(0, 0, 320 - POST_ICON_SIZE - SMALL_MARGIN * 2, VIDEO_LIST_THUMBNAIL_HEIGHT * 0.8));
		res->additional_video_view
			->set_title_lines(truncate_str(post.video.title, res->additional_video_view->get_title_width() - SMALL_MARGIN, 2, 0.5, 0.5))
			->set_thumbnail_url(post.video.thumbnail_url)
			->set_auxiliary_lines({post.video.author})
			->set_bottom_right_overlay(post.video.duration_text)
			->set_get_background_color(View::STANDARD_BACKGROUND)
			->set_on_view_released([video_url] (View &view) { clicked_url = video_url; });
	}
	
	return res;
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
		add_cpu_limit(ADDITIONAL_CPU_LIMIT);
		result = youtube_parse_channel_page(url);
		remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	}
	
	// wrap and truncate here to avoid taking time in locked state
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
	View *new_playlist_view = NULL;
	if (!result.has_playlist_to_load()) new_playlist_view = get_playlist_categories_tab_view(result.playlists);
	std::vector<View *> new_community_posts_view;
	for (auto post : result.community_posts)
		new_community_posts_view.push_back(community_post_2_view(post));
	Util_log_save("channel", "truncate end");
	
	// update metadata if it's subscribed
	if (subscription_is_subscribed(result.id) && result.name != "") {
		SubscriptionChannel new_info;
		new_info.id = result.id;
		new_info.url = result.url;
		new_info.name = result.name;
		new_info.icon_url = result.icon_url;
		new_info.subscriber_count_str = result.subscriber_count_str;
		subscription_unsubscribe(result.id);
		subscription_subscribe(new_info);
		
		misc_tasks_request(TASK_SAVE_SUBSCRIPTION);
		var_need_reflesh = true;
	}
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
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
	tab_view->views[1]->recursive_delete_subviews();
	delete tab_view->views[1];
	if (result.has_playlist_to_load()) {
		tab_view->views[1] = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))
			->set_text((std::function<std::string ()>) [] () { return channel_info.error != "" ? channel_info.error : LOCALIZED(LOADING); })
			->set_x_alignment(TextView::XAlign::CENTER)
			->set_on_drawn([] (const View &) {
				if (channel_info.error == "" && channel_info.has_playlist_to_load()) {
					if (!is_async_task_running(load_channel) &&
						!is_async_task_running(load_channel_playlists)) queue_async_task(load_channel_playlists, NULL);
				}
			});
	} else tab_view->views[1] = new_playlist_view; // possible if the channel info is loaded from cache
	// community post
	for (auto view : community_thumbnail_loaded_list) view->cancel_all_thumbnail_requests();
	community_thumbnail_loaded_list.clear();
	community_post_list_view->recursive_delete_subviews();
	community_post_list_view->set_views(new_community_posts_view);
	if (result.has_community_posts_to_load()) {
		community_post_load_more_view
			->update_y_range(0, DEFAULT_FONT_INTERVAL * 2)
			->set_is_visible(true);
	} else {
		community_post_load_more_view
			->update_y_range(0, 0)
			->set_is_visible(false);
	}
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
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
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
	auto *playlist_tab_view = get_playlist_categories_tab_view(new_result.playlists);
	Util_log_save("channel-p", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
	if (new_result.error != "") channel_info.error = new_result.error;
	else {
		channel_info = new_result;
		channel_info_cache[channel_info.url_original] = channel_info;
	}
	tab_view->views[1]->recursive_delete_subviews();
	delete tab_view->views[1];
	tab_view->views[1] = playlist_tab_view;
	
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static void load_channel_community_posts(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto prev_result = channel_info;
	svcReleaseMutex(resource_lock);
	
	auto new_result = youtube_channel_load_community(prev_result);
	
	Util_log_save("channel-com", "truncate start");
	std::vector<View *> new_post_views;
	for (size_t i = prev_result.community_posts.size(); i < new_result.community_posts.size(); i++)
		new_post_views.push_back(community_post_2_view(new_result.community_posts[i]));
	Util_log_save("channel-com", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (exiting) { // app shut down while loading
		svcReleaseMutex(resource_lock);
		return;
	}
	if (new_result.error != "") channel_info.error = new_result.error;
	else {
		channel_info = new_result;
		channel_info_cache[channel_info.url_original] = channel_info;
	}
	community_post_list_view->views.insert(community_post_list_view->views.end(), new_post_views.begin(), new_post_views.end());
	if (new_result.has_community_posts_to_load()) {
		community_post_load_more_view
			->update_y_range(0, DEFAULT_FONT_INTERVAL * 2)
			->set_is_visible(true);
	} else {
		community_post_load_more_view
			->update_y_range(0, 0)
			->set_is_visible(false);
	}
	
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
		if (var_debug_mode) Draw_debug_info();
		
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
		
		// community post thumbnail requests update
		if (channel_info.community_posts.size()) {
			std::vector<std::pair<float, PostView *> > should_be_loaded;
			{
				constexpr int LOW = -1000;
				constexpr int HIGH = 1240;
				float cur_y = community_post_y;
				for (size_t i = 0; i < community_post_list_view->views.size(); i++) {
					float cur_height = community_post_list_view->views[i]->get_height();
					if (cur_y < HIGH && cur_y + cur_height >= LOW) {
						auto parent_post_view = dynamic_cast<PostView *>(community_post_list_view->views[i]);
						if (cur_y + parent_post_view->get_self_height() >= LOW) should_be_loaded.push_back({cur_y, parent_post_view});
						auto list = parent_post_view->get_reply_pos_list(); // {y offset, reply view}
						for (auto j : list) {
							float cur_reply_height = j.second->get_height();
							if (cur_y + j.first < HIGH && cur_y + j.first + cur_reply_height > LOW) should_be_loaded.push_back({cur_y + j.first, j.second});
						}
					}
					cur_y += cur_height;
				}
				if (should_be_loaded.size() > MAX_THUMBNAIL_LOAD_REQUEST) {
					int leftover = should_be_loaded.size() - MAX_THUMBNAIL_LOAD_REQUEST;
					should_be_loaded.erase(should_be_loaded.begin(), should_be_loaded.begin() + leftover / 2);
					should_be_loaded.erase(should_be_loaded.end() - (leftover - leftover / 2), should_be_loaded.end());
				}
			}
			
			std::set<PostView *> newly_loading_views, cancelling_views;
			for (auto i : community_thumbnail_loaded_list) cancelling_views.insert(i);
			for (auto i : should_be_loaded) newly_loading_views.insert(i.second);
			for (auto i : community_thumbnail_loaded_list) newly_loading_views.erase(i);
			for (auto i : should_be_loaded) cancelling_views.erase(i.second);
			
			for (auto i : cancelling_views) {
				i->cancel_all_thumbnail_requests();
				community_thumbnail_loaded_list.erase(i);
			}
			for (auto i : newly_loading_views) {
				i->author_icon_handle = thumbnail_request(i->author_icon_url, SceneType::CHANNEL, 0, ThumbnailType::ICON);
				if (i->additional_image_url != "") i->additional_image_handle = thumbnail_request(i->additional_image_url, SceneType::CHANNEL, 0, ThumbnailType::DEFAULT);
				if (i->additional_video_view) i->additional_video_view->thumbnail_handle = 
					thumbnail_request(i->additional_video_view->thumbnail_url, SceneType::CHANNEL, 0, ThumbnailType::VIDEO_THUMBNAIL);
				community_thumbnail_loaded_list.insert(i);
			}
			
			std::vector<std::pair<int, int> > priority_list;
			auto priority = [&] (float i) { return 500 + (i < 0 ? i : 240 - i) / 100; };
			for (auto i : should_be_loaded) {
				priority_list.push_back({i.second->author_icon_handle, priority(i.first)});
				if (i.second->additional_image_handle != -1) priority_list.push_back({i.second->additional_image_handle, priority(i.first)});
				if (i.second->additional_video_view) priority_list.push_back({i.second->additional_video_view->thumbnail_handle, priority(i.first)});
			}
			thumbnail_set_priorities(priority_list);
		}
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
