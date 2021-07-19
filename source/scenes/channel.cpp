#include "headers.hpp"
#include <vector>
#include <numeric>

#include "scenes/channel.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/scroller.hpp"

#define THUMBNAIL_HEIGHT 54
#define THUMBNAIL_WIDTH 96
#define VIDEOS_MARGIN 6
#define VIDEOS_VERTICAL_INTERVAL (THUMBNAIL_HEIGHT + VIDEOS_MARGIN)
#define LOAD_MORE_MARGIN 30
#define BANNER_HEIGHT 55
#define ICON_SIZE 55
#define SMALL_MARGIN 5
#define TAB_SELECTOR_HEIGHT 20
#define TAB_SELECTOR_SELECTED_LINE_HEIGHT 3

#define MAX_THUMBNAIL_LOAD_REQUEST 30

#define TAB_NUM 2

namespace Channel {
	std::string string_resource[DEF_CHANNEL_NUM_OF_MSG];
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	int VIDEO_LIST_Y_HIGH = 240;
	VerticalScroller videos_scroller = VerticalScroller(0, 320, 0, VIDEO_LIST_Y_HIGH);
	
	int selected_tab = 0;
	
	Handle resource_lock;
	std::string cur_channel_url;
	YouTubeChannelDetail channel_info;
	int thumbnail_request_l = 0;
	int thumbnail_request_r = 0;
	std::vector<int> thumbnail_handles;
	int banner_thumbnail_handle = -1;
	int icon_thumbnail_handle = -1;
	std::vector<std::vector<std::string> > wrapped_titles;
	
	const std::string tab_strings[TAB_NUM] = {"Videos", "Info"};
};
using namespace Channel;

static void reset_channel_info() {
	for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) thumbnail_cancel_request(thumbnail_handles[i]);
	thumbnail_handles.clear();
	thumbnail_request_l = thumbnail_request_r = 0;
	if (icon_thumbnail_handle != -1) thumbnail_cancel_request(icon_thumbnail_handle), icon_thumbnail_handle = -1;
	if (banner_thumbnail_handle != -1) thumbnail_cancel_request(banner_thumbnail_handle), banner_thumbnail_handle = -1;
	channel_info = YouTubeChannelDetail();
}
static void on_channel_load() { // this will be called while resource_lock is locked
	thumbnail_handles.assign(channel_info.videos.size(), -1);
	if (channel_info.icon_url != "") icon_thumbnail_handle = thumbnail_request(channel_info.icon_url, SceneType::CHANNEL, 1001, ThumbnailType::ICON);
	if (channel_info.banner_url != "") banner_thumbnail_handle = thumbnail_request(channel_info.banner_url, SceneType::CHANNEL, 1000, ThumbnailType::VIDEO_BANNER);
	var_need_reflesh = true;
}
static void on_channel_load_more() {
	thumbnail_handles.resize(channel_info.videos.size(), -1);
	var_need_reflesh = true;
}

static bool send_load_request(std::string url) {
	if (!is_webpage_loading_requested(LoadRequestType::CHANNEL)) {
		cancel_webpage_loading(LoadRequestType::CHANNEL_CONTINUE);
		
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		
		cur_channel_url = url;
		reset_channel_info();
		
		ChannelLoadRequestArg *request = new ChannelLoadRequestArg;
		request->lock = resource_lock;
		request->result = &channel_info;
		request->url = url;
		request->max_width = 320 - (THUMBNAIL_WIDTH + 3);
		request->text_size_x = 0.5;
		request->text_size_y = 0.5;
		request->wrapped_titles = &wrapped_titles;
		request->on_load_complete = on_channel_load;
		request_webpage_loading(LoadRequestType::CHANNEL, request);
		
		svcReleaseMutex(resource_lock);
		return true;
	} else return false;
}
static bool send_load_more_request() {
	bool res = false;
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (channel_info.videos.size() && channel_info.has_continue()) {
		ChannelLoadRequestArg *request = new ChannelLoadRequestArg;
		request->lock = resource_lock;
		request->result = &channel_info;
		request->max_width = 320 - (THUMBNAIL_WIDTH + 3);
		request->text_size_x = 0.5;
		request->text_size_y = 0.5;
		request->wrapped_titles = &wrapped_titles;
		request->on_load_complete = on_channel_load_more;
		request_webpage_loading(LoadRequestType::CHANNEL_CONTINUE, request);
		res = true;
	}
	svcReleaseMutex(resource_lock);
	return res;
}

bool Channel_query_init_flag(void) {
	return already_init;
}


void Channel_resume(std::string arg)
{
	if (arg != "" && arg != cur_channel_url) send_load_request(arg);
	thread_suspend = false;
	var_need_reflesh = true;
}

void Channel_suspend(void)
{
	thread_suspend = true;
}

void Channel_init(void)
{
	Util_log_save("channel/init", "Initializing...");
	Result_with_string result;
	
	reset_channel_info();
	svcCreateMutex(&resource_lock, false);
	
	// result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SEARCH_NUM_OF_MSG);
	// Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	Channel_resume("");
	already_init = true;
}

void Channel_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	/*
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(search_thread, time_out));
	threadFree(search_thread);*/

	Util_log_save("search/exit", "Exited.");
}


struct TemporaryCopyOfChannelInfo {
	std::string error;
	std::string name;
	std::string banner_url;
	std::string icon_url;
	std::string description;
	int video_num;
	int displayed_l;
	int displayed_r;
	std::map<int, YouTubeVideoSuccinct> videos;
	std::map<int, std::vector<std::string> > wrapped_titles;
	bool has_continue;
};
static void draw_channel_content(TemporaryCopyOfChannelInfo &channel_info, Hid_info key, int color) {
	if (is_webpage_loading_requested(LoadRequestType::CHANNEL)) {
		Draw("Loading", 0, 0, 0.5, 0.5, color);
	} else  {
		int y_offset = -videos_scroller.get_offset();
		if (channel_info.banner_url != "") {
			thumbnail_draw(banner_thumbnail_handle, 0, y_offset, 320, BANNER_HEIGHT); // banner
			y_offset += BANNER_HEIGHT;
		}
		y_offset += SMALL_MARGIN;
		if (channel_info.icon_url != "") {
			thumbnail_draw(icon_thumbnail_handle, SMALL_MARGIN, y_offset, ICON_SIZE, ICON_SIZE); // icon
		}
		Draw(channel_info.name, ICON_SIZE + SMALL_MARGIN * 3, y_offset - 3, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, color); // channel name
		y_offset += ICON_SIZE;
		y_offset += SMALL_MARGIN;
		
		// tab selector
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, 0, y_offset, 320, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], DEF_DRAW_GRAY, selected_tab * 320 / TAB_NUM, y_offset, 320 / TAB_NUM + 1, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], DEF_DRAW_DARK_GRAY, selected_tab * 320 / TAB_NUM, y_offset + TAB_SELECTOR_HEIGHT - TAB_SELECTOR_SELECTED_LINE_HEIGHT,
			320 / TAB_NUM + 1, TAB_SELECTOR_SELECTED_LINE_HEIGHT);
		for (int i = 0; i < TAB_NUM; i++) {
			float font_size = 0.4;
			float center = (1 + 2 * i) * 320 / (2 * TAB_NUM);
			float width = Draw_get_width(tab_strings[i], font_size, font_size);
			float y = y_offset + 3;
			if (i == selected_tab) y -= 1;
			Draw(tab_strings[i], center - width / 2, y, font_size, font_size, DEF_DRAW_BLACK);
		}
		y_offset += TAB_SELECTOR_HEIGHT;
		
		if (selected_tab == 0) { // videos
			if (channel_info.video_num) {
				for (int i = channel_info.displayed_l; i < channel_info.displayed_r; i++) {
					int y_l = y_offset + i * VIDEOS_VERTICAL_INTERVAL;
					int y_r = y_l + THUMBNAIL_HEIGHT;
					
					if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r) {
						u8 darkness = std::min<int>(0xFF, 0xD0 + (1 - videos_scroller.selected_overlap_darkness()) * 0x30);
						u32 color = 0xFF000000 | darkness << 16 | darkness << 8 | darkness;
						Draw_texture(var_square_image[0], color, 0, y_l, 320, y_r - y_l);
					}
					
					auto cur_video = channel_info.videos[i];
					// title
					auto title_lines = channel_info.wrapped_titles[i];
					for (size_t line = 0; line < title_lines.size(); line++) {
						Draw(title_lines[line], THUMBNAIL_WIDTH + 3, y_l + (int) line * 13, 0.5, 0.5, color);
					}
					// thumbnail
					thumbnail_draw(thumbnail_handles[i], 0, y_l, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
				}
				y_offset += channel_info.video_num * VIDEOS_VERTICAL_INTERVAL;
				if (channel_info.error != "" || channel_info.has_continue) {
					std::string draw_string = "";
					if (is_webpage_loading_requested(LoadRequestType::CHANNEL_CONTINUE)) draw_string = "Loading...";
					else if (channel_info.error != "") draw_string = channel_info.error;
					
					if (y_offset < VIDEO_LIST_Y_HIGH) {
						int width = Draw_get_width(draw_string, 0.5, 0.5);
						Draw(draw_string, (320 - width) / 2, y_offset, 0.5, 0.5, color);
					}
					y_offset += LOAD_MORE_MARGIN;
				}
			} else Draw("Empty", 0, y_offset, 0.5, 0.5, color);
		} else if (selected_tab == 1) { // channel description
			Draw("Channel Description :", 3, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, color);
			y_offset += Draw_get_height("Channel Description :", MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			y_offset += 3; // without this, the following description somehow overlaps with the above text
			Draw(channel_info.description, 3, y_offset, 0.5, 0.5, color);
			y_offset += Draw_get_height(channel_info.description, 0.5, 0.5);
			y_offset += SMALL_MARGIN;
			Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 320 - 1 - SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 1);
			y_offset += SMALL_MARGIN;
		}
	}
}


Intent Channel_draw(void)
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
	
	thumbnail_set_active_scene(SceneType::CHANNEL);
	
	bool video_playing_bar_show = video_is_playing();
	VIDEO_LIST_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	videos_scroller.change_area(0, 320, 0, VIDEO_LIST_Y_HIGH);
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	int video_num = channel_info.videos.size();
	int displayed_l, displayed_r;
	{
		
		int y_offset = 0;
		if (channel_info.banner_url != "") y_offset += BANNER_HEIGHT;
		y_offset += ICON_SIZE + SMALL_MARGIN * 2 + TAB_SELECTOR_HEIGHT;
		displayed_l = std::min(video_num, std::max(0, (videos_scroller.get_offset() - y_offset) / VIDEOS_VERTICAL_INTERVAL));
		displayed_r = std::min(video_num, std::max(0, (videos_scroller.get_offset() - y_offset + VIDEO_LIST_Y_HIGH - 1) / VIDEOS_VERTICAL_INTERVAL + 1));
	}
	// back up some information while `resource_lock` is locked
	TemporaryCopyOfChannelInfo channel_info_bak;
	channel_info_bak.error = channel_info.error;
	channel_info_bak.name = channel_info.name;
	channel_info_bak.icon_url = channel_info.icon_url;
	channel_info_bak.banner_url = channel_info.banner_url;
	channel_info_bak.description = channel_info.description;
	channel_info_bak.has_continue = channel_info.has_continue();
	channel_info_bak.video_num = video_num;
	channel_info_bak.displayed_l = displayed_l;
	channel_info_bak.displayed_r = displayed_r;
	for (int i = displayed_l; i < displayed_r; i++) {
		channel_info_bak.videos[i] = channel_info.videos[i];
		channel_info_bak.wrapped_titles[i] = wrapped_titles[i];
	}
	
	// thumbnail request update (this should be done while `resource_lock` is locked)
	if (channel_info.videos.size()) {
		
		int request_target_l = std::max(0, displayed_l - (MAX_THUMBNAIL_LOAD_REQUEST - (displayed_r - displayed_l)) / 2);
		int request_target_r = std::min(video_num, request_target_l + MAX_THUMBNAIL_LOAD_REQUEST);
		// transition from [thumbnail_request_l, thumbnail_request_r) to [request_target_l, request_target_r)
		std::set<int> new_indexes, cancelling_indexes;
		for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) cancelling_indexes.insert(i);
		for (int i = request_target_l; i < request_target_r; i++) new_indexes.insert(i);
		for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) new_indexes.erase(i);
		for (int i = request_target_l; i < request_target_r; i++) cancelling_indexes.erase(i);
		
		for (auto i : cancelling_indexes) thumbnail_cancel_request(thumbnail_handles[i]), thumbnail_handles[i] = -1;
		for (auto i : new_indexes) thumbnail_handles[i] = thumbnail_request(channel_info.videos[i].thumbnail_url, SceneType::CHANNEL, 0, ThumbnailType::VIDEO_THUMBNAIL);
		
		thumbnail_request_l = request_target_l;
		thumbnail_request_r = request_target_r;
		
		std::vector<std::pair<int, int> > priority_list(request_target_r - request_target_l);
		auto dist = [&] (int i) { return i < displayed_l ? displayed_l - i : i - displayed_r + 1; };
		for (int i = request_target_l; i < request_target_r; i++) priority_list[i - request_target_l] = {thumbnail_handles[i], 500 - dist(i)};
		thumbnail_set_priorities(priority_list);
	}
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
		
		draw_channel_content(channel_info_bak, key, color);
		
		if (video_playing_bar_show) video_draw_playing_bar();
		
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
		int content_height = 0;
		if (channel_info_bak.banner_url != "") content_height += BANNER_HEIGHT;
		content_height += ICON_SIZE + SMALL_MARGIN * 2 + TAB_SELECTOR_HEIGHT;
		if (selected_tab == 0) {
			content_height += channel_info_bak.video_num * VIDEOS_VERTICAL_INTERVAL;
			// load more
			if (content_height - videos_scroller.get_offset() < VIDEO_LIST_Y_HIGH && !is_webpage_loading_requested(LoadRequestType::CHANNEL_CONTINUE) &&
				channel_info_bak.video_num) {
				send_load_more_request();
			}
			if (channel_info_bak.error != "" || channel_info_bak.has_continue) content_height += LOAD_MORE_MARGIN;
		} else if (selected_tab == 1) {
			content_height += Draw_get_height("Channel Description :", MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			content_height += 3;
			content_height += Draw_get_height(channel_info_bak.description, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			content_height += 3 + 3;
		}
		auto released_point = videos_scroller.update(key, content_height);
		
		if (video_playing_bar_show) video_update_playing_bar(key);
		// handle touches
		if (released_point.second != -1) do {
			int released_x = released_point.first;
			int released_y = released_point.second;
			int y_offset = 0;
			if (channel_info_bak.banner_url != "") y_offset += BANNER_HEIGHT;
			y_offset += ICON_SIZE + SMALL_MARGIN * 2;
			
			if (y_offset <= released_y && released_y < y_offset + TAB_SELECTOR_HEIGHT) {
				int next_tab_index = released_x * TAB_NUM / 320;
				selected_tab = next_tab_index;
				var_need_reflesh = true;
				break;
			}
			y_offset += TAB_SELECTOR_HEIGHT;
			if (selected_tab == 0) {
				if (y_offset <= released_y && released_y < y_offset + (int) channel_info_bak.video_num * VIDEOS_VERTICAL_INTERVAL) {
					int index = (released_y - y_offset) / VIDEOS_VERTICAL_INTERVAL;
					int remainder = (released_y - y_offset) % VIDEOS_VERTICAL_INTERVAL;
					if (remainder < THUMBNAIL_HEIGHT) {
						if (displayed_l <= index && index < displayed_r) {
							intent.next_scene = SceneType::VIDEO_PLAYER;
							intent.arg = channel_info_bak.videos[index].url;
							break;
						} else Util_log_save("channel", "unexpected : a video that is not displayed is selected");
					}
				}
				y_offset += channel_info_bak.video_num * VIDEOS_VERTICAL_INTERVAL;
				if (channel_info_bak.error != "" || channel_info_bak.has_continue) y_offset += LOAD_MORE_MARGIN;
			} else {
				y_offset += Draw_get_height("Channel Description :", MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
				y_offset += 3;
				y_offset += Draw_get_height(channel_info_bak.description, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
				y_offset += SMALL_MARGIN + SMALL_MARGIN;
				// nothing to do on the description
			}
		} while (0);
		
		if (key.p_b) {
			intent.next_scene = SceneType::SEARCH;
		}
		if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
