#include "headers.hpp"
#include <vector>
#include <numeric>

#include "scenes/channel.hpp"
#include "youtube_parser/parser.hpp"

#define VIDEO_LIST_Y_HIGH 220
#define VIDEOS_VERTICAL_INTERVAL 60
#define THUMBNAIL_HEIGHT 54
#define THUMBNAIL_WIDTH 96
#define BANNER_HEIGHT 55
#define ICON_SIZE 55
#define ICON_MARGIN 5
#define TAB_SELECTOR_HEIGHT 20
#define TAB_SELECTOR_SELECTED_LINE_HEIGHT 3


#define TAB_NUM 2

#define MIDDLE_FONT_SIZE 0.641

namespace Channel {
	std::string string_resource[DEF_CHANNEL_NUM_OF_MSG];
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	VerticalScroller videos_scroller = VerticalScroller(0, 320, 0, VIDEO_LIST_Y_HIGH);
	
	int selected_tab = 0;
	
	Handle resource_lock;
	std::string cur_channel_url;
	YouTubeChannelDetail channel_info;
	std::vector<std::vector<std::string> > wrapped_titles;
};
using namespace Channel;

static void reset_channel_info() {
	for (auto i : channel_info.videos)
		cancel_request_thumbnail(i.thumbnail_url);
	if (channel_info.banner_url != "") cancel_request_thumbnail(channel_info.banner_url);
	if (channel_info.icon_url != "") cancel_request_thumbnail(channel_info.icon_url);
	channel_info = YouTubeChannelDetail();
}


static bool send_load_request(std::string url) {
	if (!is_webpage_loading_requested(LoadRequestType::CHANNEL)) {
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
		request->on_load_complete = NULL;
		request_webpage_loading(LoadRequestType::CHANNEL, request);
		
		svcReleaseMutex(resource_lock);
		return true;
	} else return false;
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


static void draw_channel_content(const YouTubeChannelDetail &channel_info, const std::vector<std::vector<std::string> > &wrapped_titles, Hid_info key, int color) {
	if (is_webpage_loading_requested(LoadRequestType::SEARCH)) {
		Draw("Loading", 0, 0, 0.5, 0.5, color);
	} else  {
		int y_offset = 0;
		if (channel_info.banner_url != "") {
			draw_thumbnail(channel_info.banner_url, 0, y_offset - videos_scroller.get_offset(), 320, BANNER_HEIGHT);
			y_offset += BANNER_HEIGHT;
		}
		y_offset += ICON_MARGIN;
		if (channel_info.icon_url != "") {
			draw_thumbnail(channel_info.icon_url, ICON_MARGIN, y_offset - videos_scroller.get_offset(), ICON_SIZE, ICON_SIZE);
		}
		Draw(channel_info.name, ICON_SIZE + ICON_MARGIN * 3, y_offset - videos_scroller.get_offset() - 3, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, color);
		y_offset += ICON_SIZE;
		y_offset += ICON_MARGIN;
		
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, 0, y_offset - videos_scroller.get_offset(), 320, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], DEF_DRAW_GRAY, selected_tab * 320 / TAB_NUM, y_offset - videos_scroller.get_offset(), 320 / TAB_NUM + 1, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], DEF_DRAW_DARK_GRAY, selected_tab * 320 / TAB_NUM, y_offset - videos_scroller.get_offset() + TAB_SELECTOR_HEIGHT - TAB_SELECTOR_SELECTED_LINE_HEIGHT,
			320 / TAB_NUM + 1, TAB_SELECTOR_SELECTED_LINE_HEIGHT);
		y_offset += TAB_SELECTOR_HEIGHT;
		
		if (selected_tab == 0) { // videos
			if (channel_info.videos.size()) {
				for (size_t i = 0; i < channel_info.videos.size(); i++) {
					int y_l = y_offset + i * VIDEOS_VERTICAL_INTERVAL - videos_scroller.get_offset();
					int y_r = y_l + VIDEOS_VERTICAL_INTERVAL;
					if (y_r <= 0 || y_l >= VIDEO_LIST_Y_HIGH) continue;
					
					if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r && videos_scroller.is_selecting()) {
						Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 0, y_l, 320, VIDEOS_VERTICAL_INTERVAL);
					}
					
					auto cur_video = channel_info.videos[i];
					// title
					auto title_lines = wrapped_titles[i];
					for (size_t line = 0; line < title_lines.size(); line++) {
						Draw(title_lines[line], THUMBNAIL_WIDTH + 3, y_l + line * 13, 0.5, 0.5, color);
					}
					// thumbnail
					draw_thumbnail(cur_video.thumbnail_url, 0, y_l, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
				
				}
			} else Draw("Empty", 0, y_offset, 0.5, 0.5, color);
		} else if (selected_tab == 1) { // channel description
			Draw("Channel Description :", 3, y_offset - videos_scroller.get_offset(), MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, color);
			y_offset += Draw_get_height("Channel Description :", MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			y_offset += 3; // without this, the following description somehow overlaps with the above text
			Draw(channel_info.description, 3, y_offset - videos_scroller.get_offset(), 0.5, 0.5, color);
			y_offset += Draw_get_height(channel_info.description, 0.5, 0.5);
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
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto channel_info_bak = channel_info;
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
		
		Draw_screen_ready(1, back_color);
		
		draw_channel_content(channel_info_bak, wrapped_titles_bak, key, color);
		
		Draw_texture(var_square_image[0], color, 0, VIDEO_LIST_Y_HIGH, 320, 1);
		Draw_texture(var_square_image[0], back_color, 0, VIDEO_LIST_Y_HIGH + 1, 320, 240 - VIDEO_LIST_Y_HIGH - 1);
		
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
		content_height += ICON_SIZE + ICON_MARGIN * 2 + TAB_SELECTOR_HEIGHT;
		if (selected_tab == 0) content_height += channel_info_bak.videos.size() * VIDEOS_VERTICAL_INTERVAL;
		else if (selected_tab == 1) {
			content_height += Draw_get_height("Channel Description :", MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			content_height += 3;
			content_height += Draw_get_height(channel_info_bak.description, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
		}
		auto released_point = videos_scroller.update(key, content_height);
		
		if (released_point.second != -1) do {
			int released_x = released_point.first;
			int released_y = released_point.second;
			int y_offset = 0;
			if (channel_info_bak.banner_url != "") y_offset += BANNER_HEIGHT;
			y_offset += ICON_SIZE + ICON_MARGIN * 2;
			
			if (y_offset <= released_y && released_y < y_offset + TAB_SELECTOR_HEIGHT) {
				int next_tab_index = released_x * TAB_NUM / 320;
				selected_tab = next_tab_index;
				var_need_reflesh = true;
				break;
			}
			y_offset += TAB_SELECTOR_HEIGHT;
			if (selected_tab == 0) {
				if (y_offset <= released_y && released_y < y_offset + (int) channel_info_bak.videos.size() * VIDEOS_VERTICAL_INTERVAL) {
					int index = (released_y - y_offset) / VIDEOS_VERTICAL_INTERVAL;
					intent.next_scene = SceneType::VIDEO_PLAYER;
					intent.arg = channel_info_bak.videos[index].url;
					break;
				}
			} else {
				content_height += Draw_get_height("Channel Description :", MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
				content_height += 3;
				content_height += Draw_get_height(channel_info_bak.description, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
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
