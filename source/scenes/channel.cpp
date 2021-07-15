#include "headers.hpp"
#include <vector>
#include <numeric>

#include "scenes/channel.hpp"
#include "youtube_parser/parser.hpp"

#define VIDEO_LIST_Y_LOW 40
#define VIDEO_LIST_Y_HIGH 220
#define VIDEOS_VERTICAL_INTERVAL 60
#define THUMBNAIL_HEIGHT 54
#define THUMBNAIL_WIDTH 96

namespace Channel {
	std::string string_resource[DEF_CHANNEL_NUM_OF_MSG];
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	Handle resource_lock;
	std::string cur_channel_url;
	YouTubeChannelDetail channel_info;
	std::vector<std::vector<std::string> > wrapped_titles;
};
using namespace Channel;


static bool send_load_request(std::string url) {
	if (!is_webpage_loading_requested(LoadRequestType::CHANNEL)) {
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		
		cur_channel_url = url;
		for (auto i : channel_info.videos)
			cancel_request_thumbnail(i.thumbnail_url);
		channel_info = YouTubeChannelDetail(); // reset to empty results
		// reset_hid_state();
		// scroll_offset = 0;
		
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
	send_load_request(arg);
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
		Draw("Loading", 0, VIDEO_LIST_Y_LOW, 0.5, 0.5, color);
	} else if (channel_info.videos.size()) {
		for (size_t i = 0; i < channel_info.videos.size(); i++) {
			int y_l = VIDEO_LIST_Y_LOW + i * VIDEOS_VERTICAL_INTERVAL; // - scroll_offset;
			int y_r = VIDEO_LIST_Y_LOW + (i + 1) * VIDEOS_VERTICAL_INTERVAL; // - scroll_offset;
			if (y_r <= VIDEO_LIST_Y_LOW || y_l >= VIDEO_LIST_Y_HIGH) continue;
			
			/*
			if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r && list_grabbed && !list_scrolling) {
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 0, y_l, 320, VIDEOS_VERTICAL_INTERVAL);
			}*/
			
			auto cur_video = channel_info.videos[i];
			// title
			auto title_lines = wrapped_titles[i];
			for (size_t line = 0; line < title_lines.size(); line++) {
				Draw(title_lines[line], THUMBNAIL_WIDTH + 3, y_l + line * 13, 0.5, 0.5, color);
			}
			// thumbnail
			draw_thumbnail(cur_video.thumbnail_url, 0, y_l, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
		
		}
	} else Draw("Empty", 0, VIDEO_LIST_Y_LOW, 0.5, 0.5, color);
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
		if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
