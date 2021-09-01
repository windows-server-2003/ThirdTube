#include "headers.hpp"
#include <vector>
#include <numeric>
#include <math.h>

#include "scenes/video_player.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "ui/ui.hpp"
#include "network/network_io.hpp"
#include "network/network_decoder_multiple.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/async_task.hpp"
#include "system/util/util.hpp"

#define NEW_3DS_CPU_LIMIT 50
#define OLD_3DS_CPU_LIMIT 80

#define ICON_SIZE 55
#define TAB_SELECTOR_HEIGHT 20
#define TAB_SELECTOR_SELECTED_LINE_HEIGHT 3
#define SUGGESTION_THUMBNAIL_HEIGHT 54
#define SUGGESTION_THUMBNAIL_WIDTH 96
#define SUGGESTIONS_VERTICAL_INTERVAL (SUGGESTION_THUMBNAIL_HEIGHT + SMALL_MARGIN)
#define SUGGESTION_LOAD_MORE_MARGIN 30
#define COMMENT_ICON_SIZE 48
#define COMMENT_LOAD_MORE_MARGIN 30

#define MAX_THUMBNAIL_LOAD_REQUEST 30
#define MAX_RETRY_CNT 5

#define TAB_NUM 4
#define TAB_GENERAL 0
#define TAB_COMMENTS 1
#define TAB_SUGGESTIONS 2
#define TAB_ADVANCED 3

namespace VideoPlayer {
	bool vid_main_run = false;
	bool vid_thread_run = false;
	bool vid_already_init = false;
	bool vid_thread_suspend = true;
	volatile bool vid_play_request = false;
	volatile bool vid_seek_request = false;
	volatile bool vid_change_video_request = false;
	bool vid_detail_mode = false;
	volatile bool vid_pausing = false;
	volatile bool vid_pausing_seek = false;
	bool vid_linear_filter = true;
	bool vid_show_controls = false;
	bool vid_allow_skip_frames = false;
	double vid_time[2][320];
	double vid_copy_time[2] = { 0, 0, };
	double vid_audio_time = 0;
	double vid_video_time = 0;
	double vid_convert_time = 0;
	double vid_frametime = 0;
	double vid_framerate = 0;
	int vid_sample_rate = 0;
	double vid_duration = 0;
	double vid_zoom = 1;
	double vid_x = 0;
	double vid_y = 15;
	double vid_current_pos = 0;
	volatile double vid_seek_pos = 0;
	double vid_min_time = 0;
	double vid_max_time = 0;
	double vid_total_time = 0;
	double vid_recent_time[90];
	double vid_recent_total_time = 0;
	int vid_total_frames = 0;
	int vid_width = 0;
	int vid_width_org = 0;
	int vid_height = 0;
	int vid_height_org = 0;
	int vid_tex_width[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int vid_tex_height[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int vid_lr_count = 0;
	int vid_cd_count = 0;
	int vid_mvd_image_num = 0;
	std::string vid_url = "";
	std::string vid_video_format = "n/a";
	std::string vid_audio_format = "n/a";
	Image_data vid_image[8];
	int icon_thumbnail_handle = -1;
	C2D_Image vid_banner[2];
	C2D_Image vid_control[2];
	Thread vid_decode_thread, vid_convert_thread;
	
	VerticalScroller scroller[TAB_NUM];
	constexpr int CONTENT_Y_HIGH = 240 - TAB_SELECTOR_HEIGHT - VIDEO_PLAYING_BAR_HEIGHT; // the bar is always shown here, so this is a constant
	VerticalScroller tab_selector_scroller; // special one, it does not actually scroll but handles touch releasing
	int selected_tab = 0;

	Thread stream_downloader_thread;
	NetworkStreamDownloader stream_downloader;
	
	Thread livestream_initer_thread;
	NetworkMultipleDecoder network_decoder;
	Handle network_decoder_critical_lock; // locked when seeking or deiniting
	
	Handle small_resource_lock; // locking basically all std::vector, std::string, etc
	YouTubeVideoDetail cur_video_info;
	int video_retry_left = 0;
	int suggestion_thumbnail_request_l = 0;
	int suggestion_thumbnail_request_r = 0;
	std::vector<int> suggestion_thumbnail_handles;
	std::vector<std::vector<std::string> > suggestion_titles_lines;
	std::set<CommentView *> comment_thumbnail_loaded_list;
	std::vector<std::string> title_lines;
	float title_font_size;
	std::vector<std::string> description_lines;
	
	std::string channel_url_pressed;
	
	View *comments_top_view = new EmptyView(0, 0, 320, 4);
	ListView *comments_main_view = new ListView(0, 0, 320);
	View *comments_bottom_view = new EmptyView(0, 0, 320, 0);
	ScrollView *comment_all_view = NULL;
};
using namespace VideoPlayer;

static const char * volatile network_waiting_status = NULL;
const char *get_network_waiting_status() {
	if (network_waiting_status) return network_waiting_status;
	return network_decoder.get_network_waiting_status();
}

bool VideoPlayer_query_init_flag(void)
{
	return vid_already_init;
}

// START : functions called from async_task.cpp
/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */

#define TITLE_MAX_WIDTH (320 - SMALL_MARGIN * 2)
#define DESC_MAX_WIDTH (320 - SMALL_MARGIN * 2)
#define SUGGESTION_TITLE_MAX_WIDTH (320 - (SUGGESTION_THUMBNAIL_WIDTH + 3))
#define COMMENT_MAX_WIDTH (320 - (COMMENT_ICON_SIZE + 2 * SMALL_MARGIN))
#define REPLY_INDENT 25
#define REPLY_MAX_WIDTH (320 - REPLY_INDENT - (REPLY_ICON_SIZE + 2 * SMALL_MARGIN))


static void load_video_page(void *);
static void load_more_comments(void *);
static void load_more_suggestions(void *);
static void load_more_replies(void *);

static void update_bottom_view() {
	delete comments_bottom_view;
	if (cur_video_info.comments_disabled || cur_video_info.error != "" || cur_video_info.has_more_comments() || !cur_video_info.comments.size()) {
		TextView *bottom_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))
			->set_text(
				cur_video_info.comments_disabled ? LOCALIZED(COMMENTS_DISABLED) :
				cur_video_info.error != "" ? cur_video_info.error :
				cur_video_info.has_more_comments() ? LOCALIZED(LOADING) :
				!cur_video_info.comments.size() ? LOCALIZED(NO_COMMENTS) : "IE")
			->set_font_size(0.5, DEFAULT_FONT_INTERVAL)
			->set_x_centered(true)
			->set_y_centered(false);
		
		comment_all_view->set_on_child_drawn(2, [] (const ScrollView &, int) {
			if (cur_video_info.has_more_comments()) {
				if (!is_async_task_running(load_video_page) &&
					!is_async_task_running(load_more_comments)) queue_async_task(load_more_comments, &cur_video_info);
			}
		});
		comments_bottom_view = bottom_view;
	} else comments_bottom_view = new EmptyView(0, 0, 320, 0);
	comment_all_view->views[2] = comments_bottom_view;
}

static void load_video_page(void *arg) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	std::string url = *(const std::string *) arg;
	svcReleaseMutex(small_resource_lock);
	
	Util_log_save("player/load-v", "request : " + url);
	
	add_cpu_limit(25);
	YouTubeVideoDetail tmp_video_info = youtube_parse_video_page(url);
	remove_cpu_limit(25);
	
	Util_log_save("player/load-v", "truncate start");
	// wrap main title
	float title_font_size_tmp;
	std::vector<std::string> title_lines_tmp = truncate_str(tmp_video_info.title, TITLE_MAX_WIDTH, 3, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
	if (title_lines_tmp.size() == 3) {
		title_lines_tmp = truncate_str(tmp_video_info.title, TITLE_MAX_WIDTH, 2, 0.5, 0.5);
		title_font_size_tmp = 0.5;
	} else title_font_size_tmp = MIDDLE_FONT_SIZE;
	// wrap description
	std::vector<std::string> description_lines_tmp;
	{
		auto &description = tmp_video_info.description;
		auto itr = description.begin();
		while (itr != description.end()) {
			auto next_itr = std::find(itr, description.end(), '\n');
			auto cur_lines = truncate_str(std::string(itr, next_itr), DESC_MAX_WIDTH, 100, 0.5, 0.5);
			description_lines_tmp.insert(description_lines_tmp.end(), cur_lines.begin(), cur_lines.end());
			if (next_itr != description.end()) itr = std::next(next_itr);
			else break;
		}
	}
	// wrap suggestion titles
	std::vector<std::vector<std::string> > suggestion_titles_lines_tmp(tmp_video_info.suggestions.size());
	for (size_t i = 0; i < tmp_video_info.suggestions.size(); i++)
		suggestion_titles_lines_tmp[i] = truncate_str(tmp_video_info.suggestions[i].title, SUGGESTION_TITLE_MAX_WIDTH, 2, 0.5, 0.5);
	Util_log_save("player/load-v", "truncate end");
	
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	cur_video_info = tmp_video_info;
	description_lines = description_lines_tmp;
	suggestion_titles_lines = suggestion_titles_lines_tmp;
	title_lines = title_lines_tmp;
	title_font_size = title_font_size_tmp;
	
	// cancel thumbnail requests
	for (auto view : comment_thumbnail_loaded_list) {
		thumbnail_cancel_request(view->author_icon_handle);
		view->author_icon_handle = -1;
	}
	comment_thumbnail_loaded_list.clear();
	comments_main_view->recursive_delete_subviews();
	
	update_bottom_view();
	
	thumbnail_cancel_requests(suggestion_thumbnail_handles);
	suggestion_thumbnail_request_l = suggestion_thumbnail_request_r = 0;
	suggestion_thumbnail_handles.assign(cur_video_info.suggestions.size(), -1);
	
	thumbnail_cancel_request(icon_thumbnail_handle);
	icon_thumbnail_handle = thumbnail_request(cur_video_info.author.icon_url, SceneType::VIDEO_PLAYER, 1000, ThumbnailType::ICON);
	
	for (int i = 0; i < TAB_NUM; i++) scroller[i].reset();
	var_need_reflesh = true;
	
	if (cur_video_info.is_playable()) {
		vid_change_video_request = true;
		if (network_decoder.ready) network_decoder.interrupt = true;
	}
	svcReleaseMutex(small_resource_lock);
}
static void load_more_suggestions(void *arg_) {
	YouTubeVideoDetail *arg = (YouTubeVideoDetail *) arg_;
	
	add_cpu_limit(25);
	auto new_result = youtube_video_page_load_more_suggestions(*arg);
	remove_cpu_limit(25);
	
	// wrap suggestion titles
	Util_log_save("player/load-s", "truncate start");
	std::vector<std::vector<std::string> > suggestion_titles_lines_add(new_result.suggestions.size() - arg->suggestions.size());
	for (size_t i = arg->suggestions.size(); i < new_result.suggestions.size(); i++)
		suggestion_titles_lines_add[i - arg->suggestions.size()] = truncate_str(new_result.suggestions[i].title, SUGGESTION_TITLE_MAX_WIDTH, 2, 0.5, 0.5);
	Util_log_save("player/load-s", "truncate end");
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") cur_video_info.error = new_result.error;
	else {
		cur_video_info = new_result;
		suggestion_titles_lines.insert(suggestion_titles_lines.end(), suggestion_titles_lines_add.begin(), suggestion_titles_lines_add.end());
	}
	suggestion_thumbnail_handles.resize(cur_video_info.suggestions.size(), -1);
	var_need_reflesh = true;
	svcReleaseMutex(small_resource_lock);
}

#define COMMENT_MAX_LINE_NUM 1000 // this limit exists due to performance reason (TODO : more efficient truncating)

static void load_more_comments(void *arg_) {
	YouTubeVideoDetail *arg = (YouTubeVideoDetail *) arg_;
	
	add_cpu_limit(25);
	auto new_result = youtube_video_page_load_more_comments(*arg);
	remove_cpu_limit(25);
	
	std::vector<View *> new_comment_views;
	// wrap comments
	Util_log_save("player/load-c", "truncate start");
	for (size_t i = arg->comments.size(); i < new_result.comments.size(); i++) {
		auto &cur_comment = new_result.comments[i];
		auto &cur_content = cur_comment.content;
		std::vector<std::string> cur_lines;
		auto itr = cur_content.begin();
		while (itr != cur_content.end()) {
			if (cur_lines.size() >= COMMENT_MAX_LINE_NUM) break;
			auto next_itr = std::find(itr, cur_content.end(), '\n');
			auto tmp = truncate_str(std::string(itr, next_itr), COMMENT_MAX_WIDTH, COMMENT_MAX_LINE_NUM - cur_lines.size(), 0.5, 0.5);
			cur_lines.insert(cur_lines.end(), tmp.begin(), tmp.end());
			
			if (next_itr != cur_content.end()) itr = std::next(next_itr);
			else break;
		}
		new_comment_views.push_back((new CommentView(0, 0, 320))
			->set_content_lines(cur_lines)
			->set_get_yt_comment_object([i](const CommentView &) -> YouTubeVideoDetail::Comment & { return cur_video_info.comments[i]; })
			->set_on_author_icon_pressed([] (const CommentView &view) { channel_url_pressed = view.get_yt_comment_object().author.url; })
			->set_on_load_more_replies_pressed([i] (CommentView &view) {
				queue_async_task(load_more_replies, &view);
				view.is_loading_replies = true;
			})
		);
	}
	Util_log_save("player/load-c", "truncate end");
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	cur_video_info = new_result;
	comments_main_view->views.insert(comments_main_view->views.end(), new_comment_views.begin(), new_comment_views.end());
	update_bottom_view();
	var_need_reflesh = true;
	svcReleaseMutex(small_resource_lock);
}

static void load_more_replies(void *arg_) {
	CommentView *comment_view = (CommentView *) arg_;
	// only load_more_comments() can invalidate this reference, so it's safe
	YouTubeVideoDetail::Comment &comment = comment_view->get_yt_comment_object();
	
	add_cpu_limit(25);
	auto new_comment = youtube_video_page_load_more_replies(comment);
	remove_cpu_limit(25);
	
	std::vector<CommentView *> new_reply_views;
	// wrap comments
	Util_log_save("player/load-r", "truncate start");
	for (size_t i = comment.replies.size(); i < new_comment.replies.size(); i++) {
		auto &cur_reply = new_comment.replies[i];
		auto &cur_content = cur_reply.content;
		std::vector<std::string> cur_lines;
		auto itr = cur_content.begin();
		while (itr != cur_content.end()) {
			if (cur_lines.size() >= COMMENT_MAX_LINE_NUM) break;
			auto next_itr = std::find(itr, cur_content.end(), '\n');
			auto tmp = truncate_str(std::string(itr, next_itr), REPLY_MAX_WIDTH, COMMENT_MAX_LINE_NUM - cur_lines.size(), 0.5, 0.5);
			cur_lines.insert(cur_lines.end(), tmp.begin(), tmp.end());
			
			if (next_itr != cur_content.end()) itr = std::next(next_itr);
			else break;
		}
		new_reply_views.push_back((new CommentView(REPLY_INDENT, 0, 320 - REPLY_INDENT))
			->set_content_lines(cur_lines)
			->set_get_yt_comment_object([comment_view, i](const CommentView &) -> YouTubeVideoDetail::Comment & { return comment_view->get_yt_comment_object().replies[i]; })
			->set_on_author_icon_pressed([] (const CommentView &view) { channel_url_pressed = view.get_yt_comment_object().author.url; })
			->set_is_reply(true)
		);
	}
	Util_log_save("player/load-r", "truncate end");
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	comment = new_comment;
	comment_view->replies.insert(comment_view->replies.end(), new_reply_views.begin(), new_reply_views.end());
	comment_view->replies_shown = comment_view->replies.size();
	comment_view->is_loading_replies = false;
	svcReleaseMutex(small_resource_lock);
	var_need_reflesh = true;
}

/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */
// END : functions called from async_task.cpp


static bool send_load_more_suggestions_request() {
	if (is_async_task_running(load_video_page) || is_async_task_running(load_more_suggestions)) return false;
	queue_async_task(load_more_suggestions, &cur_video_info);
	return true;
}

static void send_change_video_request_wo_lock(std::string url) {
	remove_all_async_tasks_with_type(load_video_page);
	remove_all_async_tasks_with_type(load_more_suggestions);
	remove_all_async_tasks_with_type(load_more_comments);
	
	vid_play_request = false;
	if (vid_url != url) {
		vid_url = url;
		selected_tab = TAB_GENERAL;
	}
	queue_async_task(load_video_page, &vid_url);
	var_need_reflesh = true;
}
static void send_change_video_request(std::string url) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	send_change_video_request_wo_lock(url);
	svcReleaseMutex(small_resource_lock);
}

static void send_seek_request(double pos) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	vid_seek_pos = pos;
	vid_current_pos = pos / 1000;
	vid_seek_request = true;
	if (network_decoder.ready) // avoid locking while initing
		network_decoder.interrupt = true;
	svcReleaseMutex(small_resource_lock);
}


bool video_is_playing() {
	if (!vid_play_request && vid_pausing_seek) {
		svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
		vid_pausing_seek = false;
		if (!vid_pausing) Util_speaker_resume(0);
		svcReleaseMutex(small_resource_lock);
	}
	return vid_play_request;
}
#define SMALL_FONT_SIZE 0.4

namespace Bar {
	bool bar_grabbed = false;
	double last_grab_timestamp = 0;
	int last_touch_x = -1;
	int last_touch_y = -1;
	float bar_x_l, bar_x_r;
	constexpr int MAXIMIZE_ICON_WIDTH = 28;
	constexpr int TIME_STR_RIGHT_MARGIN = 2;
	constexpr int TIME_STR_LEFT_MARGIN = 3;
	void video_draw_playing_bar() {
		// draw
		float y_l = 240 - VIDEO_PLAYING_BAR_HEIGHT;
		float y_center = 240 - (float) VIDEO_PLAYING_BAR_HEIGHT / 2;
		
		
		Draw_texture(var_square_image[0], DEF_DRAW_DARK_GRAY, 0, y_l, 320, VIDEO_PLAYING_BAR_HEIGHT);
		
		if (get_network_waiting_status()) { // loading
			C2D_DrawCircleSolid(6, y_center, 0, 2, DEF_DRAW_WHITE);
			C2D_DrawCircleSolid(12, y_center, 0, 2, DEF_DRAW_WHITE);
			C2D_DrawCircleSolid(18, y_center, 0, 2, DEF_DRAW_WHITE);
		} else if (vid_pausing || vid_pausing_seek || !vid_play_request) { // pausing
			C2D_DrawTriangle(8, y_l + 3, DEF_DRAW_WHITE,
							 8, 240 - 4, DEF_DRAW_WHITE,
							 8 + (VIDEO_PLAYING_BAR_HEIGHT - 6) * std::sqrt(3) / 2, y_center, DEF_DRAW_WHITE,
							 0);
		} else { // playing
			C2D_DrawRectSolid(8, y_l + 3, 0, 4, VIDEO_PLAYING_BAR_HEIGHT - 6, DEF_DRAW_WHITE);
			C2D_DrawRectSolid(16, y_l + 3, 0, 4, VIDEO_PLAYING_BAR_HEIGHT - 6, DEF_DRAW_WHITE);
		}
		// maximize icon
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, y_l + 3, 8, 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, y_l + 3, 3, 5);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, 240 - 6, 8, 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, 240 - 8, 3, 5);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 12, y_l + 3, 8, 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 7, y_l + 3, 3, 5);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 12, 240 - 6, 8, 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 7, 240 - 8, 3, 5);
		
		double bar_timestamp = vid_current_pos;
		if (bar_grabbed) bar_timestamp = last_grab_timestamp;
		else if (vid_seek_request) bar_timestamp = vid_current_pos;
		double vid_progress = network_decoder.get_bar_pos_from_timestamp(bar_timestamp);
		
		std::string time_str0 = Util_convert_seconds_to_time(bar_timestamp);
		std::string time_str1 = "/ " + Util_convert_seconds_to_time(vid_duration);
		float time_str0_w = Draw_get_width(time_str0, SMALL_FONT_SIZE, SMALL_FONT_SIZE);
		float time_str1_w = Draw_get_width(time_str1, SMALL_FONT_SIZE, SMALL_FONT_SIZE);
		bar_x_l = 30;
		bar_x_r = 320 - std::max(time_str0_w, time_str1_w) - TIME_STR_RIGHT_MARGIN - TIME_STR_LEFT_MARGIN - MAXIMIZE_ICON_WIDTH;
		
		
		float time_str_w = std::max(time_str0_w, time_str1_w);
		Draw(time_str0, bar_x_r + TIME_STR_LEFT_MARGIN + (time_str_w - time_str0_w) / 2, y_l - 1, SMALL_FONT_SIZE, SMALL_FONT_SIZE, DEF_DRAW_WHITE);
		Draw(time_str1, bar_x_r + TIME_STR_LEFT_MARGIN + (time_str_w - time_str1_w) / 2, y_center - 2, SMALL_FONT_SIZE, SMALL_FONT_SIZE, DEF_DRAW_WHITE);
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, bar_x_l, y_center - 2, bar_x_r - bar_x_l, 4);
		if (vid_duration != 0) {
			Draw_texture(var_square_image[0], 0xFF3333D0, bar_x_l, y_center - 2, (bar_x_r - bar_x_l) * vid_progress, 4);
			C2D_DrawCircleSolid(bar_x_l + (bar_x_r - bar_x_l) * vid_progress, y_center, 0, bar_grabbed ? 6 : 4, 0xFF3333D0);
		}
	}
	void video_update_playing_bar(Hid_info key, Intent *intent) {
		float y_l = 240 - VIDEO_PLAYING_BAR_HEIGHT;
		float y_center = 240 - (float) VIDEO_PLAYING_BAR_HEIGHT / 2;
		
		constexpr int GRAB_TOLERANCE = 5;
		if (!bar_grabbed && network_decoder.ready && key.p_touch && bar_x_l - GRAB_TOLERANCE <= key.touch_x && key.touch_x <= bar_x_r + GRAB_TOLERANCE &&
			y_center - 2 - GRAB_TOLERANCE <= key.touch_y && key.touch_y <= y_center + 2 + GRAB_TOLERANCE) {
			bar_grabbed = true;
		}
		if (bar_grabbed)
			last_grab_timestamp = network_decoder.get_timestamp_from_bar_pos(((key.touch_x != -1 ? key.touch_x : last_touch_x) - bar_x_l) / (bar_x_r - bar_x_l));
		if (bar_grabbed && key.touch_x == -1) {
			if (network_decoder.ready) send_seek_request(last_grab_timestamp * 1000);
			bar_grabbed = false;
		}
		if (key.p_touch && key.touch_x >= 320 - MAXIMIZE_ICON_WIDTH && key.touch_y >= 240 - VIDEO_PLAYING_BAR_HEIGHT && vid_thread_suspend) {
			intent->next_scene = SceneType::VIDEO_PLAYER;
			intent->arg = vid_url;
		}
		svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
		if (!vid_pausing_seek && bar_grabbed && !vid_pausing) Util_speaker_pause(0);
		if (vid_pausing_seek && !bar_grabbed && !vid_pausing) Util_speaker_resume(0);
		vid_pausing_seek = bar_grabbed;
		
		// start/stop
		if (!get_network_waiting_status() && !vid_pausing_seek && key.p_touch && key.touch_x < 22 && y_l <= key.touch_y) {
			if (vid_play_request) {
				if (vid_pausing) {
					vid_pausing = false;
					Util_speaker_resume(0);
				} else {
					vid_pausing = true;
					Util_speaker_pause(0);
				}
			} else if (cur_video_info.is_playable()) {
				network_waiting_status = NULL;
				vid_play_request = true;
			}
		}
		svcReleaseMutex(small_resource_lock);
		
		last_touch_x = key.touch_x;
		last_touch_y = key.touch_y;
	}
}
void video_update_playing_bar(Hid_info key, Intent *intent) { Bar::video_update_playing_bar(key, intent); }
void video_draw_playing_bar() { Bar::video_draw_playing_bar(); }



static void decode_thread(void* arg)
{
	Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Thread started.");

	Result_with_string result;
	int ch = 0;
	int audio_size = 0;
	int w = 0;
	int h = 0;
	double pos = 0;
	bool key = false;
	u8* audio = NULL;
	std::string format = "";
	std::string type = (char*)arg;
	TickCounter counter0, counter1;
	osTickCounterStart(&counter0);
	osTickCounterStart(&counter1);

	while (vid_thread_run)
	{
		if(vid_play_request || vid_change_video_request)
		{
			vid_x = 0;
			vid_y = 15;
			vid_frametime = 0;
			vid_framerate = 0;
			vid_current_pos = 0;
			vid_duration = 0;
			vid_zoom = 1;
			vid_width = 0;
			vid_height = 0;
			vid_mvd_image_num = 0;
			vid_video_format = "n/a";
			vid_audio_format = "n/a";
			vid_change_video_request = false;
			vid_seek_request = false;
			vid_play_request = true;
			vid_total_time = 0;
			vid_total_frames = 0;
			vid_min_time = 99999999;
			vid_max_time = 0;
			vid_recent_total_time = 0;
			for(int i = 0; i < 90; i++)
				vid_recent_time[i] = 0;
			
			for(int i = 0; i < 8; i++)
			{
				vid_tex_width[i] = 0;
				vid_tex_height[i] = 0;
			}

			for(int i = 0 ; i < 320; i++)
			{
				vid_time[0][i] = 0;
				vid_time[1][i] = 0;
			}

			vid_audio_time = 0;
			vid_video_time = 0;
			vid_copy_time[0] = 0;
			vid_copy_time[1] = 0;
			vid_convert_time = 0;
			
			/*
			result = Util_decoder_open_file(vid_dir + vid_file, &has_audio, &has_video, 0);
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_decoder_open_file()..." + result.string + result.error_description, result.code);
			*/
			
			// video page parsing sometimes randomly fails, so try several times
			network_waiting_status = "Reading Stream";
			// itag 18 (both_stream) of a long video takes too much time and sometimes leads to a crash 
			if (cur_video_info.duration_ms <= 60 * 60 * 1000 && cur_video_info.both_stream_url != "") {
				result = network_decoder.init(cur_video_info.both_stream_url, stream_downloader,
					cur_video_info.is_livestream ? cur_video_info.stream_fragment_len : -1, cur_video_info.needs_timestamp_adjusting(), true);
			} else if (cur_video_info.video_stream_url != "" && cur_video_info.audio_stream_url != "") { 
				result = network_decoder.init(cur_video_info.video_stream_url, cur_video_info.audio_stream_url, stream_downloader,
					cur_video_info.is_livestream ? cur_video_info.stream_fragment_len : -1, cur_video_info.needs_timestamp_adjusting(), true);
			} else {
				result.code = -1;
				result.string = "YouTube parser error";
				result.error_description = "No valid stream url extracted";
			}
			
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "network_decoder.init()..." + result.string + result.error_description, result.code);
			if(result.code != 0) {
				if (video_retry_left > 0) {
					video_retry_left--;
					Util_log_save("dec", "failed, retrying. retry cnt left:" + std::to_string(video_retry_left));
					send_change_video_request(vid_url);
				} else {
					Util_err_set_error_message(result.string, result.error_description, DEF_SAPP0_DECODE_THREAD_STR, result.code);
					Util_err_set_error_show_flag(true);
					var_need_reflesh = true;
				}
				vid_play_request = false;
			}
			network_waiting_status = NULL;
			
			// result = Util_decoder_open_network_stream(network_cacher_data, &has_audio, &has_video, 0);
			
			if (vid_play_request) {
				{
					auto tmp = network_decoder.get_audio_info();
					// bitrate = tmp.bitrate;
					vid_sample_rate = tmp.sample_rate;
					ch = tmp.ch;
					vid_audio_format = tmp.format_name;
					vid_duration = tmp.duration;
				}
				Util_speaker_init(0, ch, vid_sample_rate);
				{
					auto tmp = network_decoder.get_video_info();
					vid_width = vid_width_org = tmp.width;
					vid_height = vid_height_org = tmp.height;
					vid_framerate = tmp.framerate;
					vid_video_format = tmp.format_name;
					vid_duration = tmp.duration;
					vid_frametime = 1000.0 / vid_framerate;;

					if(vid_width % 16 != 0)
						vid_width += 16 - vid_width % 16;
					if(vid_height % 16 != 0)
						vid_height += 16 - vid_height % 16;

					//fit to screen size
					while(((vid_width * vid_zoom) > 400 || (vid_height * vid_zoom) > 225) && vid_zoom > 0.05)
						vid_zoom -= 0.001;

					vid_x = (400 - (vid_width * vid_zoom)) / 2;
					vid_y = (225 - (vid_height * vid_zoom)) / 2;
					vid_y += 15;
				}
			}
			
			osTickCounterUpdate(&counter1);
			while (vid_play_request)
			{
				if (vid_seek_request && !vid_change_video_request) {
					network_waiting_status = "Seeking";
					vid_current_pos = vid_seek_pos / 1000;
					Util_speaker_clear_buffer(0);
					svcWaitSynchronization(network_decoder_critical_lock, std::numeric_limits<s64>::max()); // the converter thread is now suspended
					while (vid_seek_request && !vid_change_video_request && vid_play_request) {
						svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
						double seek_pos_bak = vid_seek_pos;
						vid_seek_request = false;
						network_decoder.interrupt = false;
						svcReleaseMutex(small_resource_lock);
						
						result = network_decoder.seek(seek_pos_bak * 1000);
						// if seek failed because of the lock, it's probably another seek request or other requests (change video etc...), so we can ignore it
						if (network_decoder.interrupt) {
							Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "seek interrupted");
							continue; 
						}
						Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "network_decoder.seek()..." + result.string + result.error_description, result.code);
						if (result.code != 0) vid_play_request = false;
						break;
					}
					svcReleaseMutex(network_decoder_critical_lock);
					network_waiting_status = NULL;
				}
				if (vid_change_video_request || !vid_play_request) break;
				vid_duration = network_decoder.get_duration();
				
				auto type = network_decoder.next_decode_type();
				if (type == NetworkMultipleDecoder::DecodeType::AUDIO) {
					osTickCounterUpdate(&counter0);
					result = network_decoder.decode_audio(&audio_size, &audio, &pos);
					osTickCounterUpdate(&counter0);
					vid_audio_time = osTickCounterRead(&counter0);
					
					if (!std::isnan(pos) && !std::isinf(pos))
						vid_current_pos = pos;
					
					if(result.code == 0)
					{
						while(true)
						{
							result = Util_speaker_add_buffer(0, ch, audio, audio_size, pos);
							if(result.code == 0 || !vid_play_request || vid_seek_request || vid_change_video_request)
								break;
							// Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "audio queue full");
							
							usleep(10000);
						}
					}
					else
						Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_audio_decoder_decode()..." + result.string + result.error_description, result.code);

					free(audio);
					audio = NULL;
				} else if (type == NetworkMultipleDecoder::DecodeType::VIDEO) {
					osTickCounterUpdate(&counter0);
					result = network_decoder.decode_video(&w, &h, &key, &pos);
					osTickCounterUpdate(&counter0);
					
					// Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "decoded a video packet at " + std::to_string(pos));
					while (result.code == DEF_ERR_NEED_MORE_OUTPUT && vid_play_request && !vid_seek_request && !vid_change_video_request) {
						usleep(10000);
						osTickCounterUpdate(&counter0);
						result = network_decoder.decode_video(&w, &h, &key, &pos);
						osTickCounterUpdate(&counter0);
					}
					vid_video_time = osTickCounterRead(&counter0);
					
					// get the elapsed time from the previous frame
					osTickCounterUpdate(&counter1);
					double cur_frame_internval = osTickCounterRead(&counter1);
					
					vid_min_time = std::min(vid_min_time, cur_frame_internval);
					vid_max_time = std::max(vid_max_time, cur_frame_internval);
					vid_total_time += cur_frame_internval;
					vid_total_frames++;
					vid_recent_time[89] = cur_frame_internval;
					vid_recent_total_time = std::accumulate(vid_recent_time, vid_recent_time + 90, 0.0);
					for (int i = 1; i < 90; i++) vid_recent_time[i - 1] = vid_recent_time[i];
					
					vid_time[0][319] = cur_frame_internval;
					for (int i = 1; i < 320; i++) vid_time[0][i - 1] = vid_time[0][i];
					
					if (vid_play_request && !vid_seek_request && !vid_change_video_request) {
						if (result.code != 0)
							Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_video_decoder_decode()..." + result.string + result.error_description, result.code);
					}
				} else if (type == NetworkMultipleDecoder::DecodeType::EoF) break;
				else if (type == NetworkMultipleDecoder::DecodeType::INTERRUPTED) continue;
				else Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "unknown type of packet");
			}
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "decoding end, waiting for the speaker to cease playing...");
			
			network_waiting_status = NULL;
			
			while (Util_speaker_is_playing(0) && vid_play_request) usleep(10000);
			Util_speaker_exit(0);
			
			if(!vid_change_video_request)
				vid_play_request = false;
			
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "speaker exited, waiting for convert thread");
			// make sure the convert thread stops before closing network_decoder
			svcWaitSynchronization(network_decoder_critical_lock, std::numeric_limits<s64>::max()); // the converter thread is now suspended
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "speaker exited, deinit...");
			network_decoder.deinit();
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "speaker exited, deinit finish");
			svcReleaseMutex(network_decoder_critical_lock);
			
			var_need_reflesh = true;
			vid_pausing = false;
			vid_seek_request = false;
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "deinit complete");
		}
		else
			usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);

		while (vid_thread_suspend && !vid_play_request && !vid_change_video_request)
			usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
	}
	
	Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Thread exit.");
	threadExit(0);
}

static void convert_thread(void* arg)
{
	Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Thread started.");
	u8* yuv_video = NULL;
	u8* video = NULL;
	TickCounter counter0, counter1;
	Result_with_string result;

	osTickCounterStart(&counter0);

	while (vid_thread_run)
	{
		if (vid_play_request && !vid_seek_request && !vid_change_video_request)
		{
			svcWaitSynchronization(network_decoder_critical_lock, std::numeric_limits<s64>::max());
			while(vid_play_request && !vid_seek_request && !vid_change_video_request)
			{
				double pts;
				do {
					osTickCounterUpdate(&counter1);
					osTickCounterUpdate(&counter0);
					result = network_decoder.get_decoded_video_frame(vid_width, vid_height, network_decoder.hw_decoder_enabled ? &video : &yuv_video, &pts);
					osTickCounterUpdate(&counter0);
					if (result.code != DEF_ERR_NEED_MORE_INPUT) break;
					if (vid_pausing || vid_pausing_seek) usleep(10000);
					else usleep(3000);
				} while (vid_play_request && !vid_seek_request && !vid_change_video_request);
				
				
				if (!vid_play_request || vid_seek_request || vid_change_video_request) break;
				if (result.code != 0) { // this is an unexpected error
					Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "failure getting decoded result" + result.string + result.error_description, result.code);
					vid_play_request = false;
					break;
				}
				
				bool video_need_free = false;
				vid_copy_time[0] = osTickCounterRead(&counter0);
				
				osTickCounterUpdate(&counter0);
				if (!network_decoder.hw_decoder_enabled) {
					result = Util_converter_yuv420p_to_bgr565(yuv_video, &video, vid_width, vid_height);
					video_need_free = true;
				}
				osTickCounterUpdate(&counter0);
				vid_convert_time = osTickCounterRead(&counter0);
				
				double cur_convert_time = 0;
				
				if(result.code == 0)
				{
					if(vid_width > 1024 && vid_height > 1024)
					{
						vid_tex_width[vid_mvd_image_num * 4 + 0] = 1024;
						vid_tex_width[vid_mvd_image_num * 4 + 1] = vid_width - 1024;
						vid_tex_width[vid_mvd_image_num * 4 + 2] = 1024;
						vid_tex_width[vid_mvd_image_num * 4 + 3] = vid_width - 1024;
						vid_tex_height[vid_mvd_image_num * 4 + 0] = 1024;
						vid_tex_height[vid_mvd_image_num * 4 + 1] = 1024;
						vid_tex_height[vid_mvd_image_num * 4 + 2] = vid_height - 1024;
						vid_tex_height[vid_mvd_image_num * 4 + 3] = vid_height - 1024;
					}
					else if(vid_width > 1024)
					{
						vid_tex_width[vid_mvd_image_num * 4 + 0] = 1024;
						vid_tex_width[vid_mvd_image_num * 4 + 1] = vid_width - 1024;
						vid_tex_height[vid_mvd_image_num * 4 + 0] = vid_height;
						vid_tex_height[vid_mvd_image_num * 4 + 1] = vid_height;
					}
					else if(vid_height > 1024)
					{
						vid_tex_width[vid_mvd_image_num * 4 + 0] = vid_width;
						vid_tex_width[vid_mvd_image_num * 4 + 1] = vid_width;
						vid_tex_height[vid_mvd_image_num * 4 + 0] = 1024;
						vid_tex_height[vid_mvd_image_num * 4 + 1] = vid_height - 1024;
					}
					else
					{
						vid_tex_width[vid_mvd_image_num * 4 + 0] = vid_width;
						vid_tex_height[vid_mvd_image_num * 4 + 0] = vid_height;
					}
					
					// we don't want to include the sleep time in the performance profiling
					osTickCounterUpdate(&counter0);
					vid_copy_time[1] = osTickCounterRead(&counter0);
					osTickCounterUpdate(&counter1);
					cur_convert_time = osTickCounterRead(&counter1);
					
					// sync with sound
					double cur_sound_pos = Util_speaker_get_current_timestamp(0, vid_sample_rate);
					// Util_log_save("conv", "pos : " + std::to_string(pts) + " / " + std::to_string(cur_sound_pos));
					if (cur_sound_pos < 0) { // sound is not playing, probably because the video is lagging behind, so draw immediately
						
					} else {
						while (pts - cur_sound_pos > 0.003 && vid_play_request && !vid_seek_request && !vid_change_video_request) {
							usleep((pts - cur_sound_pos - 0.0015) * 1000000);
							cur_sound_pos = Util_speaker_get_current_timestamp(0, vid_sample_rate);
							if (cur_sound_pos < 0) break;
						}
					}
					
					osTickCounterUpdate(&counter0);
					osTickCounterUpdate(&counter1);
					
					result = Draw_set_texture_data(&vid_image[vid_mvd_image_num * 4 + 0], video, vid_width, vid_height_org, 1024, 1024, GPU_RGB565);
					if(result.code != 0)
						Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);

					if(vid_width > 1024)
					{
						result = Draw_set_texture_data(&vid_image[vid_mvd_image_num * 4 + 1], video, vid_width, vid_height_org, 1024, 0, 1024, 1024, GPU_RGB565);
						if(result.code != 0)
							Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
					}
					if(vid_height > 1024)
					{
						result = Draw_set_texture_data(&vid_image[vid_mvd_image_num * 4 + 2], video, vid_width, vid_height_org, 0, 1024, 1024, 1024, GPU_RGB565);
						if(result.code != 0)
							Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
					}
					if(vid_width > 1024 && vid_height > 1024)
					{
						result = Draw_set_texture_data(&vid_image[vid_mvd_image_num * 4 + 3], video, vid_width, vid_height_org, 1024, 1024, 1024, 1024, GPU_RGB565);
						if(result.code != 0)
							Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
					}

					if (network_decoder.hw_decoder_enabled) {
						vid_mvd_image_num = !vid_mvd_image_num;
					}

					osTickCounterUpdate(&counter0);
					vid_copy_time[1] += osTickCounterRead(&counter0);
					
					var_need_reflesh = true;
				}
				else
					Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Util_converter_yuv420p_to_bgr565()..." + result.string + result.error_description, result.code);

				if (video_need_free) free(video);
				video = NULL;
				yuv_video = NULL; // this is the result of network_decoder.get_decoded_video_frame(), so it should not be freed
				
				osTickCounterUpdate(&counter1);
				cur_convert_time += osTickCounterRead(&counter1);
				vid_time[1][319] = cur_convert_time;
				for(int i = 1; i < 320; i++)
					vid_time[1][i - 1] = vid_time[1][i];
			}
			svcReleaseMutex(network_decoder_critical_lock);
		} else usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);

		while (vid_thread_suspend && !vid_play_request && !vid_change_video_request)
			usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
	}
	
	Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Thread exit.");
	threadExit(0);
}

void VideoPlayer_resume(std::string arg)
{
	if (arg != "") {
		if (arg != vid_url) {
			send_change_video_request(arg);
			video_retry_left = MAX_RETRY_CNT;
		} else if (!vid_play_request && cur_video_info.is_playable()) vid_play_request = true;
	}
	for (auto i : scroller) i.on_resume();
	overlay_menu_on_resume();
	vid_thread_suspend = false;
	vid_main_run = true;
	var_need_reflesh = true;
}

void VideoPlayer_suspend(void)
{
	vid_thread_suspend = true;
	vid_main_run = false;
}

void VideoPlayer_init(void)
{
	Util_log_save(DEF_SAPP0_INIT_STR, "Initializing...");
	bool new_3ds = false;
	Result_with_string result;
	
	vid_thread_run = true;
	
	svcCreateMutex(&network_decoder_critical_lock, false);
	svcCreateMutex(&small_resource_lock, false);
	
	for (int i = 0; i < TAB_NUM; i++) scroller[i] = VerticalScroller(0, 320, 0, CONTENT_Y_HIGH);
	tab_selector_scroller = VerticalScroller(0, 320, CONTENT_Y_HIGH, CONTENT_Y_HIGH + TAB_SELECTOR_HEIGHT);
	comment_all_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH))->set_views({comments_top_view, comments_main_view, comments_bottom_view});
	
	APT_CheckNew3DS(&new_3ds);
	if(new_3ds)
	{
		add_cpu_limit(NEW_3DS_CPU_LIMIT);
		vid_decode_thread = threadCreate(decode_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 2, false);
		vid_convert_thread = threadCreate(convert_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	}
	else
	{
		add_cpu_limit(OLD_3DS_CPU_LIMIT);
		vid_decode_thread = threadCreate(decode_thread, (void*)("1"), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 1, false);
		vid_convert_thread = threadCreate(convert_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	}
	stream_downloader = NetworkStreamDownloader();
	stream_downloader_thread = threadCreate(network_downloader_thread, &stream_downloader, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	livestream_initer_thread = threadCreate(livestream_initer_thread_func, &network_decoder, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 2, false);

	/*
	add_cpu_limit(25);
	for (int i = 100; i < 120; i++) {
		NetworkStream *stream0 = new NetworkStream("https://r2---sn-ogueln7k.googlevideo.com/videoplayback?expire=1628243182&ei=jrAMYaznF6eL1d8Pgc-4-A4&ip=221.240.44.218&id=9gEuOzFKTt4.1&itag=134&aitags=133%2C134%2C135%2C136%2C137%2C160&source=yt_live_broadcast&requiressl=yes&mh=7y&mm=44%2C29&mn=sn-ogueln7k%2Csn-oguesnz6&ms=lva%2Crdu&mv=m&mvi=2&pl=16&initcwndbps=1841250&vprv=1&live=1&hang=1&noclen=1&mime=video%2Fmp4&ns=7YYWlOV7L86Uc8EHIVOGpRoG&gir=yes&mt=1628220986&fvip=2&keepalive=yes&fexp=24001373%2C24007246&c=MWEB&n=d1fLqBuQlw7UXA&sparams=expire%2Cei%2Cip%2Cid%2Caitags%2Csource%2Crequiressl%2Cvprv%2Clive%2Chang%2Cnoclen%2Cmime%2Cns%2Cgir&sig=AOq0QJ8wRQIhAMSzW6dgmKMMoJZxWNWq-mC0s4nJW0ycrzgefubpf19bAiAQ58yRXQO_S1uVQNOWUm1l1cC_h0zDGUPH0WIm_mKbJA%3D%3D&lsparams=mh%2Cmm%2Cmn%2Cms%2Cmv%2Cmvi%2Cpl%2Cinitcwndbps&lsig=AG3C_xAwRAIgKbF0jRdXFoFrsKlLbfk08fCteud5Pflzl_rdk68Uc2sCIA5Zo625JwLXVQZ1ixI9CQVHuzIvlI2RK9T0U-30IC4V&ratebypass=yes&sq=" + std::to_string(i), true);
		NetworkStream *stream1 = new NetworkStream("https://r2---sn-ogueln7k.googlevideo.com/videoplayback?expire=1628243182&ei=jrAMYaznF6eL1d8Pgc-4-A4&ip=221.240.44.218&id=9gEuOzFKTt4.1&itag=140&source=yt_live_broadcast&requiressl=yes&mh=7y&mm=44%2C29&mn=sn-ogueln7k%2Csn-oguesnz6&ms=lva%2Crdu&mv=m&mvi=2&pl=16&initcwndbps=1841250&vprv=1&live=1&hang=1&noclen=1&mime=audio%2Fmp4&ns=7YYWlOV7L86Uc8EHIVOGpRoG&gir=yes&mt=1628220986&fvip=2&keepalive=yes&fexp=24001373%2C24007246&c=MWEB&n=d1fLqBuQlw7UXA&sparams=expire%2Cei%2Cip%2Cid%2Citag%2Csource%2Crequiressl%2Cvprv%2Clive%2Chang%2Cnoclen%2Cmime%2Cns%2Cgir&sig=AOq0QJ8wRQIhAO9QK6ZWrt6wCG8rvv10YGn5sjbQdHW4EFrn11wcMlwHAiBKglnuc9FVgZKyzatEFHc6_BDnx-fMW2TtJdrBrgx1Gg%3D%3D&lsparams=mh%2Cmm%2Cmn%2Cms%2Cmv%2Cmvi%2Cpl%2Cinitcwndbps&lsig=AG3C_xAwRAIgKbF0jRdXFoFrsKlLbfk08fCteud5Pflzl_rdk68Uc2sCIA5Zo625JwLXVQZ1ixI9CQVHuzIvlI2RK9T0U-30IC4V&ratebypass=yes&sq=" + std::to_string(i), true);
		stream_downloader.add_stream(stream0);
		stream_downloader.add_stream(stream1);
		Util_log_save("test", "r0");
		int x = 0;
		while (!stream0->ready && !stream0->error) {
			// for (int j = 0; j < 50; j++) x ^= i * j;
			// usleep(4);
			usleep(3000);
		}
		Util_log_save("test", "r1");
		while (!stream1->ready && !stream0->error) {
			// for (int j = 0; j < 50; j++) x ^= i * j;
			// usleep(4);
			usleep(3000);
		}
		Util_log_save("test", "r2" + std::to_string(x));
		stream0->quit_request = true;
		stream1->quit_request = true;
	}
	remove_cpu_limit(25);*/

	vid_total_time = 0;
	vid_total_frames = 0;
	vid_min_time = 99999999;
	vid_max_time = 0;
	vid_recent_total_time = 0;
	for(int i = 0; i < 90; i++)
		vid_recent_time[i] = 0;

	for(int i = 0; i < 8; i++)
	{
		vid_tex_width[i] = 0;
		vid_tex_height[i] = 0;
	}

	for(int i = 0 ; i < 320; i++)
	{
		vid_time[0][i] = 0;
		vid_time[1][i] = 0;
	}

	vid_audio_time = 0;
	vid_video_time = 0;
	vid_copy_time[0] = 0;
	vid_copy_time[1] = 0;
	vid_convert_time = 0;

	for(int i = 0 ; i < 8; i++)
	{
		result = Draw_c2d_image_init(&vid_image[i], 1024, 1024, GPU_RGB565);
		if(result.code != 0)
		{
			Util_err_set_error_message(DEF_ERR_OUT_OF_LINEAR_MEMORY_STR, "", DEF_SAPP0_INIT_STR, DEF_ERR_OUT_OF_LINEAR_MEMORY);
			Util_err_set_error_show_flag(true);
			vid_thread_run = false;
		}
	}

	result = Draw_load_texture("romfs:/gfx/draw/video_player/banner.t3x", 61, vid_banner, 0, 2);
	Util_log_save(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);

	result = Draw_load_texture("romfs:/gfx/draw/video_player/control.t3x", 62, vid_control, 0, 2);
	Util_log_save(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);

	vid_detail_mode = false;
	vid_show_controls = false;
	vid_allow_skip_frames = false;
	vid_lr_count = 0;
	vid_cd_count = 0;
	vid_x = 0;
	vid_y = 15;
	vid_frametime = 0;
	vid_framerate = 0;
	vid_current_pos = 0;
	vid_duration = 0;
	vid_zoom = 1;
	vid_width = 0;
	vid_height = 0;
	vid_video_format = "n/a";
	vid_audio_format = "n/a";
	
	VideoPlayer_resume("");
	vid_already_init = true;
	Util_log_save(DEF_SAPP0_INIT_STR, "Initialized.");
}

void VideoPlayer_exit(void)
{
	Util_log_save(DEF_SAPP0_EXIT_STR, "Exiting...");
	u64 time_out = 10000000000;
	Result_with_string result;

	vid_already_init = false;
	vid_thread_suspend = false;
	vid_thread_run = false;
	vid_play_request = false;
	
	stream_downloader.request_thread_exit();
	network_decoder.interrupt = true;
	network_decoder.request_thread_exit();
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(vid_decode_thread, time_out));
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(vid_convert_thread, time_out));
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(stream_downloader_thread, time_out));
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(livestream_initer_thread, time_out));
	threadFree(vid_decode_thread);
	threadFree(vid_convert_thread);
	threadFree(stream_downloader_thread);
	threadFree(livestream_initer_thread);
	stream_downloader.delete_all();
	
	bool new_3ds;
	APT_CheckNew3DS(&new_3ds);
	if (new_3ds) {
		remove_cpu_limit(NEW_3DS_CPU_LIMIT);
	} else {
		remove_cpu_limit(OLD_3DS_CPU_LIMIT);
	}

	Draw_free_texture(61);
	Draw_free_texture(62);

	for(int i = 0; i < 8; i++)
		Draw_c2d_image_free(vid_image[i]);
	
	Util_log_save(DEF_SAPP0_EXIT_STR, "Exited.");
}

static void draw_video_content(Hid_info key) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (is_async_task_running(load_video_page)) {
		Draw_x_centered(LOCALIZED(LOADING), 0, 320, 0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
	} else {
		int y_offset = -scroller[selected_tab].get_offset();
		if (selected_tab == TAB_GENERAL) {
			if (cur_video_info.title.size()) { // only draw if the metadata is loaded
				for (int i = 0; i < (int) title_lines.size(); i++) {
					Draw(title_lines[i], SMALL_MARGIN, y_offset + 15 * i, title_font_size, title_font_size, DEFAULT_TEXT_COLOR);
				}
				y_offset += 15 * title_lines.size() + SMALL_MARGIN;
				
				Draw(cur_video_info.views_str, SMALL_MARGIN, y_offset, 0.5, 0.5, LIGHT0_TEXT_COLOR);
				Draw_right(cur_video_info.publish_date, 320 - SMALL_MARGIN * 2, y_offset, 0.5, 0.5, LIGHT0_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
				
				Draw(LOCALIZED(YOUTUBE_LIKE) + ":" + cur_video_info.like_count_str + " " + LOCALIZED(YOUTUBE_DISLIKE) + ":" + cur_video_info.dislike_count_str,
					SMALL_MARGIN, y_offset, 0.5, 0.5, LIGHT0_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
				
				y_offset += SMALL_MARGIN;
				Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 320 - 1 - SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 1);
				y_offset += SMALL_MARGIN;
				
				thumbnail_draw(icon_thumbnail_handle, SMALL_MARGIN, y_offset, ICON_SIZE, ICON_SIZE);
				Draw(cur_video_info.author.name, ICON_SIZE + SMALL_MARGIN * 3, y_offset + ICON_SIZE * 0.1, 0.55, 0.55, DEFAULT_TEXT_COLOR);
				Draw(cur_video_info.author.subscribers, ICON_SIZE + SMALL_MARGIN * 3, y_offset + ICON_SIZE * 0.1 + DEFAULT_FONT_INTERVAL + SMALL_MARGIN, 0.45, 0.45, DEF_DRAW_GRAY);
				y_offset += ICON_SIZE;
				
				if (cur_video_info.is_upcoming) {
					y_offset += SMALL_MARGIN;
					Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 320 - 1 - SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 1);
					y_offset += SMALL_MARGIN;
					
					Draw(cur_video_info.playability_reason, SMALL_MARGIN, y_offset - 1, 0.5, 0.5, DEFAULT_TEXT_COLOR);
					y_offset += DEFAULT_FONT_INTERVAL;
				}
				
				y_offset += SMALL_MARGIN;
				Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 320 - 1 - SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 1);
				y_offset += SMALL_MARGIN;
				
				int description_line_num = description_lines.size();
				int description_displayed_l = std::min(description_line_num, std::max(0, -y_offset / DEFAULT_FONT_INTERVAL));
				int description_displayed_r = std::min(description_line_num, std::max(0, (-y_offset + CONTENT_Y_HIGH - 1) / DEFAULT_FONT_INTERVAL + 1));
				for (int i = description_displayed_l; i < description_displayed_r; i++) {
					Draw(description_lines[i], SMALL_MARGIN, y_offset + i * DEFAULT_FONT_INTERVAL, 0.5, 0.5, DEFAULT_TEXT_COLOR);
				}
				y_offset += description_line_num * DEFAULT_FONT_INTERVAL;
				
				y_offset += SMALL_MARGIN;
				Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 320 - 1 - SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 1);
				y_offset += SMALL_MARGIN;
			} else Draw_x_centered(LOCALIZED(EMPTY), 0, 320, 0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		} else if (selected_tab == TAB_SUGGESTIONS) {
			if (cur_video_info.suggestions.size()) {
				int suggestion_num = cur_video_info.suggestions.size();
				int suggestion_displayed_l = std::min(suggestion_num, std::max(0, scroller[TAB_SUGGESTIONS].get_offset() / SUGGESTIONS_VERTICAL_INTERVAL));
				int suggestion_displayed_r = std::min(suggestion_num, std::max(0, (scroller[TAB_SUGGESTIONS].get_offset() + CONTENT_Y_HIGH - 1) / SUGGESTIONS_VERTICAL_INTERVAL + 1));
				for (int i = suggestion_displayed_l; i < suggestion_displayed_r; i++) {
					int y_l = y_offset + i * SUGGESTIONS_VERTICAL_INTERVAL;
					int y_r = y_l + SUGGESTION_THUMBNAIL_HEIGHT;
					
					if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r) {
						u8 darkness = std::min<int>(0xFF, 0xD0 + (1 - scroller[TAB_SUGGESTIONS].selected_overlap_darkness()) * 0x30);
						if (var_night_mode) darkness = 0xFF - darkness;
						u32 color = 0xFF000000 | darkness << 16 | darkness << 8 | darkness;
						Draw_texture(var_square_image[0], color, 0, y_l, 320, y_r - y_l);
					}
					
					auto cur_video = cur_video_info.suggestions[i];
					int cur_y = y_l;
					// thumbnail
					thumbnail_draw(suggestion_thumbnail_handles[i], 0, cur_y, SUGGESTION_THUMBNAIL_WIDTH, SUGGESTION_THUMBNAIL_HEIGHT);
					// title
					auto title_lines = suggestion_titles_lines[i];
					for (size_t line = 0; line < title_lines.size(); line++) {
						Draw(title_lines[line], SUGGESTION_THUMBNAIL_WIDTH + 3, cur_y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
						cur_y += DEFAULT_FONT_INTERVAL;
					}
					cur_y += 2;
					Draw(cur_video.author, SUGGESTION_THUMBNAIL_WIDTH + 3, cur_y, 0.5, 0.5, LIGHT0_TEXT_COLOR);
					cur_y += DEFAULT_FONT_INTERVAL;
					Draw(cur_video.duration_text, SUGGESTION_THUMBNAIL_WIDTH + 3, cur_y, 0.5, 0.5, LIGHT1_TEXT_COLOR);
					cur_y += DEFAULT_FONT_INTERVAL;
				}
				y_offset += cur_video_info.suggestions.size() * SUGGESTIONS_VERTICAL_INTERVAL;
				if (cur_video_info.has_more_suggestions() || cur_video_info.error != "") {
					std::string draw_str = cur_video_info.error != "" ? cur_video_info.error : LOCALIZED(LOADING);
					if (y_offset < 240) Draw_x_centered(draw_str, 0, 320, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
					y_offset += SUGGESTION_LOAD_MORE_MARGIN;
				}
			} else Draw_x_centered(LOCALIZED(EMPTY), 0, 320, 0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		} else if (selected_tab == TAB_COMMENTS) {
			comment_all_view->draw();
		} else if (selected_tab == TAB_ADVANCED) {
			Draw(vid_video_format, 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
			Draw(vid_audio_format, 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
			Draw(std::to_string(vid_width) + "x" + std::to_string(vid_height) + "@" + std::to_string(vid_framerate).substr(0, 5) + "fps", 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
			Draw(LOCALIZED(HW_DECODER) + " : " + LOCALIZED_ENABLED_STATUS(network_decoder.hw_decoder_enabled), 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
			
			{
				const char *message = get_network_waiting_status();
				Draw(LOCALIZED(WAITING_STATUS) + " : " + std::string(message ? message : ""), 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
			}
			{
				u32 cpu_limit;
				APT_GetAppCpuTimeLimit(&cpu_limit);
				Draw(LOCALIZED(CPU_LIMIT) + " : " + std::to_string(cpu_limit) + "%", 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
			}
			if (cur_video_info.is_livestream && network_decoder.ready) {
				Draw(LOCALIZED(FORWARD_BUFFER) + " : " + std::to_string(network_decoder.get_forward_buffer()) + " s", 0, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
			}
			//controls
			/*
			Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 165, y_offset, 145, 10);
			Draw(vid_msg[2], 167.5, y_offset, 0.4, 0.4, color);*/

			//texture filter
			y_offset += SMALL_MARGIN;
			{
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 15, y_offset, 135, DEFAULT_FONT_INTERVAL);
				Draw_x_centered(LOCALIZED(VIDEO_FILTER), 15, 15 + 135, y_offset, 0.4, 0.4, DEFAULT_TEXT_COLOR);
			}
			{
				u32 button_color = is_async_task_running(load_video_page) ? DEF_DRAW_LIGHT_GRAY : DEF_DRAW_WEAK_AQUA;
				Draw_texture(var_square_image[0], button_color, 170, y_offset, 135, DEFAULT_FONT_INTERVAL);
				int width = Draw_get_width(LOCALIZED(RELOAD), 0.4, 0.4);
				Draw(LOCALIZED(RELOAD), (170.0 + 170.0 + 135.0) / 2 - width / 2, y_offset, 0.4, 0.4, DEFAULT_TEXT_COLOR);
			}
			y_offset += DEFAULT_FONT_INTERVAL;
			y_offset += SMALL_MARGIN;
			
			// stream download progress
			{
				int sample = 50;
				auto progress_bars = network_decoder.get_buffering_progress_bars(sample);
				for (size_t i = 0; i < 2; i++) {
					if (i < progress_bars.size() && progress_bars[i].second.size()) {
						auto &cur_bar = progress_bars[i].second;
						auto cur_pos = progress_bars[i].first;
						
						int sample = 50;
						int bar_len = 310;
						for (int i = 0; i < sample; i++) {
							int a = 0xFF;
							int r = 0x00 * cur_bar[i] / 100 + 0x80 * (1 - cur_bar[i] / 100);
							int g = 0x00 * cur_bar[i] / 100 + 0x80 * (1 - cur_bar[i] / 100);
							int b = 0xA0 * cur_bar[i] / 100 + 0x80 * (1 - cur_bar[i] / 100);
							int xl = 5 + bar_len * i / sample;
							int xr = 5 + bar_len * (i + 1) / sample;
							Draw_texture(var_square_image[0], a << 24 | b << 16 | g << 8 | r, xl, y_offset, xr - xl, 3);
						}
						float head_x = 5 + bar_len * cur_pos;
						Draw_texture(var_square_image[0], DEF_DRAW_RED, head_x - 1, y_offset, 3, 3);
					}
					y_offset += SMALL_MARGIN;
				}
			}
			
			y_offset += SMALL_MARGIN;
			Draw_line(SMALL_MARGIN, y_offset, DEFAULT_TEXT_COLOR, 320 - SMALL_MARGIN - 1, y_offset, DEFAULT_TEXT_COLOR, 1);
			y_offset += SMALL_MARGIN;
			
			//decoding detail
			for(int i = 0; i < 319; i++)
			{
				Draw_line(i, y_offset + 90 - vid_time[1][i], DEF_DRAW_BLUE, i + 1, y_offset + 90 - vid_time[1][i + 1], DEF_DRAW_BLUE, 1);//Thread 1
				Draw_line(i, y_offset + 90 - vid_time[0][i], DEF_DRAW_RED, i + 1, y_offset + 90 - vid_time[0][i + 1], DEF_DRAW_RED, 1);//Thread 0
			}

			Draw_line(0, y_offset + 90, DEFAULT_TEXT_COLOR, 320, y_offset + 90, DEFAULT_TEXT_COLOR, 2);
			Draw_line(0, y_offset + 90 - vid_frametime, 0xFFFFFF00, 320, y_offset + 90 - vid_frametime, 0xFFFFFF00, 2);
			if(vid_total_frames != 0 && vid_min_time != 0  && vid_recent_total_time != 0)
			{
				Draw("avg " + std::to_string(1000 / (vid_total_time / vid_total_frames)).substr(0, 5) + " min " + std::to_string(1000 / vid_max_time).substr(0, 5) 
				+  " max " + std::to_string(1000 / vid_min_time).substr(0, 5) + " recent avg " + std::to_string(1000 / (vid_recent_total_time / 90)).substr(0, 5) +  " fps",
				0, y_offset + 90, 0.4, 0.4, DEFAULT_TEXT_COLOR);
			}

			Draw("Deadline : " + std::to_string(vid_frametime).substr(0, 5) + "ms", 0, y_offset + 100, 0.4, 0.4, 0xFFFFFF00);
			Draw("Video decode : " + std::to_string(vid_video_time).substr(0, 5) + "ms", 0, y_offset + 110, 0.4, 0.4, DEF_DRAW_RED);
			Draw("Audio decode : " + std::to_string(vid_audio_time).substr(0, 5) + "ms", 0, y_offset + 120, 0.4, 0.4, DEF_DRAW_RED);
			//Draw("Data copy 0 : " + std::to_string(vid_copy_time[0]).substr(0, 5) + "ms", 160, 120, 0.4, 0.4, DEF_DRAW_BLUE);
			Draw("Color convert : " + std::to_string(vid_convert_time).substr(0, 5) + "ms", 160, y_offset + 110, 0.4, 0.4, DEF_DRAW_BLUE);
			Draw("Data copy 1 : " + std::to_string(vid_copy_time[1]).substr(0, 5) + "ms", 160, y_offset + 120, 0.4, 0.4, DEF_DRAW_BLUE);
			Draw("Thread 0 : " + std::to_string(vid_time[0][319]).substr(0, 6) + "ms", 0, y_offset + 130, 0.5, 0.5, DEF_DRAW_RED);
			Draw("Thread 1 : " + std::to_string(vid_time[1][319]).substr(0, 6) + "ms", 160, y_offset + 130, 0.5, 0.5, DEF_DRAW_BLUE);
			Draw("Zoom : x" + std::to_string(vid_zoom).substr(0, 5) + " X : " + std::to_string((int)vid_x) + " Y : " + std::to_string((int)vid_y), 0, y_offset + 140, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += 160;
		}
	}
	scroller[selected_tab].draw_slider_bar();
	svcReleaseMutex(small_resource_lock);
}

Intent VideoPlayer_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	int image_num = network_decoder.hw_decoder_enabled ? !vid_mvd_image_num : 0;

	thumbnail_set_active_scene(SceneType::VIDEO_PLAYER);
	
	bool video_playing_bar_show = video_is_playing();
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, network_decoder.ready ? DEF_DRAW_BLACK : DEFAULT_BACK_COLOR);

		if(network_decoder.ready)
		{
			//video
			Draw_texture(vid_image[image_num * 4 + 0].c2d, vid_x, vid_y, vid_tex_width[image_num * 4 + 0] * vid_zoom, vid_tex_height[image_num * 4 + 0] * vid_zoom);
			if(vid_width > 1024)
				Draw_texture(vid_image[image_num * 4 + 1].c2d, (vid_x + vid_tex_width[image_num * 4 + 0] * vid_zoom), vid_y, vid_tex_width[image_num * 4 + 1] * vid_zoom, vid_tex_height[image_num * 4 + 1] * vid_zoom);
			if(vid_height > 1024)
				Draw_texture(vid_image[image_num * 4 + 2].c2d, vid_x, (vid_y + vid_tex_width[image_num * 4 + 0] * vid_zoom), vid_tex_width[image_num * 4 + 2] * vid_zoom, vid_tex_height[image_num * 4 + 2] * vid_zoom);
			if(vid_width > 1024 && vid_height > 1024)
				Draw_texture(vid_image[image_num * 4 + 3].c2d, (vid_x + vid_tex_width[image_num * 4 + 0] * vid_zoom), (vid_y + vid_tex_height[image_num * 4 + 0] * vid_zoom), vid_tex_width[image_num * 4 + 3] * vid_zoom, vid_tex_height[image_num * 4 + 3] * vid_zoom);
		}
		else
			Draw_texture(vid_banner[var_night_mode], 0, 15, 400, 225);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();

		Draw_screen_ready(1, DEFAULT_BACK_COLOR);

		draw_video_content(key);
			
			// Draw(DEF_SAPP0_VER, 0, 0, 0.4, 0.4, DEF_DRAW_GREEN);

			//codec info
			// TODO : move these to debug tab
			
			/*
			if(vid_play_request)
			{
				//video
				Draw_texture(vid_image[image_num * 4 + 0].c2d, vid_x - 40, vid_y - 240, vid_tex_width[image_num * 4 + 0] * vid_zoom, vid_tex_height[image_num * 4 + 0] * vid_zoom);
				if(vid_width > 1024)
					Draw_texture(vid_image[image_num * 4 + 1].c2d, (vid_x + vid_tex_width[image_num * 4 + 0] * vid_zoom) - 40, vid_y - 240, vid_tex_width[image_num * 4 + 1] * vid_zoom, vid_tex_height[image_num * 4 + 1] * vid_zoom);
				if(vid_height > 1024)
					Draw_texture(vid_image[image_num * 4 + 2].c2d, vid_x - 40, (vid_y + vid_tex_width[image_num * 4 + 0] * vid_zoom) - 240, vid_tex_width[image_num * 4 + 2] * vid_zoom, vid_tex_height[image_num * 4 + 2] * vid_zoom);
				if(vid_width > 1024 && vid_height > 1024)
					Draw_texture(vid_image[image_num * 4 + 3].c2d, (vid_x + vid_tex_width[image_num * 4 + 0] * vid_zoom) - 40, (vid_y + vid_tex_height[image_num * 4 + 0] * vid_zoom) - 240, vid_tex_width[image_num * 4 + 3] * vid_zoom, vid_tex_height[image_num * 4 + 3] * vid_zoom);
			}*/
			
			/*

			if(vid_show_controls)
			{
				Draw_texture(vid_control[var_night_mode], 80, y_offset + 20, 160, 160);
				Draw(vid_msg[5], 122.5, y_offset + 47.5, 0.45, 0.45, DEF_DRAW_BLACK);
				Draw(vid_msg[6], 122.5, y_offset + 62.5, 0.45, 0.45, DEF_DRAW_BLACK);
				Draw(vid_msg[7], 122.5, y_offset + 77.5, 0.45, 0.45, DEF_DRAW_BLACK);
				Draw(vid_msg[8], 122.5, y_offset + 92.5, 0.45, 0.45, DEF_DRAW_BLACK);
				Draw(vid_msg[9], 135, y_offset + 107.5, 0.45, 0.45, DEF_DRAW_BLACK);
				Draw(vid_msg[10], 122.5, y_offset + 122.5, 0.45, 0.45, DEF_DRAW_BLACK);
				Draw(vid_msg[11], 132.5, y_offset + 137.5, 0.45, 0.45, DEF_DRAW_BLACK);
			}
			*/
		
		// tab selector
		Draw_texture(var_square_image[0], LIGHT1_BACK_COLOR, 0, CONTENT_Y_HIGH, 320, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], LIGHT2_BACK_COLOR, selected_tab * 320 / TAB_NUM, CONTENT_Y_HIGH, 320 / TAB_NUM + 1, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], LIGHT3_BACK_COLOR, selected_tab * 320 / TAB_NUM, CONTENT_Y_HIGH, 320 / TAB_NUM + 1, TAB_SELECTOR_SELECTED_LINE_HEIGHT);
		for (int i = 0; i < TAB_NUM; i++) {
			float font_size = 0.4;
			float x_l = i * 320 / TAB_NUM;
			float x_r = (i + 1) * 320 / TAB_NUM;
			float y = CONTENT_Y_HIGH + 3;
			if (i == selected_tab) y += 1;
			std::string tab_string;
			if (i == TAB_GENERAL) tab_string = LOCALIZED(GENERAL);
			else if (i == TAB_SUGGESTIONS) tab_string = LOCALIZED(SUGGESTIONS);
			else if (i == TAB_COMMENTS) tab_string = LOCALIZED(COMMENTS);
			else if (i == TAB_ADVANCED) tab_string = LOCALIZED(ADVANCED);
			Draw_x_centered(tab_string, x_l, x_r, y, font_size, font_size, DEFAULT_TEXT_COLOR);
		}
		
		// playing bar
		video_draw_playing_bar();
		draw_overlay_menu(240 - VIDEO_PLAYING_BAR_HEIGHT - TAB_SELECTOR_HEIGHT - OVERLAY_MENU_ICON_SIZE);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();

	if(Util_err_query_error_show_flag())
		Util_err_main(key);
	else if(Util_expl_query_show_flag())
		Util_expl_main(key);
	else
	{
		/* ****************************** LOCK START ******************************  */
		svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
		
		// thumbnail request update (this should be done while `small_resource_lock` is locked)
		if (cur_video_info.suggestions.size()) { // suggestions
			int suggestion_num = cur_video_info.suggestions.size();
			int displayed_l = std::min(suggestion_num, std::max(0, scroller[TAB_SUGGESTIONS].get_offset() / SUGGESTIONS_VERTICAL_INTERVAL));
			int displayed_r = std::min(suggestion_num, std::max(0, (scroller[TAB_SUGGESTIONS].get_offset() + CONTENT_Y_HIGH - 1) / SUGGESTIONS_VERTICAL_INTERVAL + 1));
			int request_target_l = std::max(0, displayed_l - (MAX_THUMBNAIL_LOAD_REQUEST - (displayed_r - displayed_l)) / 2);
			int request_target_r = std::min(suggestion_num, request_target_l + MAX_THUMBNAIL_LOAD_REQUEST);
			// transition from [thumbnail_request_l, thumbnail_request_r) to [request_target_l, request_target_r)
			std::set<int> new_indexes, cancelling_indexes;
			for (int i = suggestion_thumbnail_request_l; i < suggestion_thumbnail_request_r; i++) cancelling_indexes.insert(i);
			for (int i = request_target_l; i < request_target_r; i++) new_indexes.insert(i);
			for (int i = suggestion_thumbnail_request_l; i < suggestion_thumbnail_request_r; i++) new_indexes.erase(i);
			for (int i = request_target_l; i < request_target_r; i++) cancelling_indexes.erase(i);
			
			for (auto i : cancelling_indexes) thumbnail_cancel_request(suggestion_thumbnail_handles[i]), suggestion_thumbnail_handles[i] = -1;
			for (auto i : new_indexes) suggestion_thumbnail_handles[i] =
				thumbnail_request(cur_video_info.suggestions[i].thumbnail_url, SceneType::VIDEO_PLAYER, 0, ThumbnailType::VIDEO_THUMBNAIL);
			
			suggestion_thumbnail_request_l = request_target_l;
			suggestion_thumbnail_request_r = request_target_r;
			
			std::vector<std::pair<int, int> > priority_list(request_target_r - request_target_l);
			auto dist = [&] (int i) { return i < displayed_l ? displayed_l - i : i - displayed_r + 1; };
			for (int i = request_target_l; i < request_target_r; i++) priority_list[i - request_target_l] = {suggestion_thumbnail_handles[i], 500 - dist(i)};
			thumbnail_set_priorities(priority_list);
		}
		if (cur_video_info.comments.size()) { // comments
			std::vector<std::pair<float, CommentView *> > comments_list; // list of comment views whose author's thumbnails should be loaded
			{
				constexpr int LOW = -1000;
				constexpr int HIGH = 1240;
				float cur_y = -comment_all_view->get_offset();
				for (size_t i = 0; i < comments_main_view->views.size(); i++) {
					float cur_height = comments_main_view->views[i]->get_height();
					if (cur_y < HIGH && cur_y + cur_height >= LOW) {
						auto parent_comment_view = dynamic_cast<CommentView *>(comments_main_view->views[i]);
						if (cur_y + parent_comment_view->get_self_height() >= LOW) comments_list.push_back({cur_y, parent_comment_view});
						auto list = parent_comment_view->get_reply_pos_list(); // {y offset, reply view}
						for (auto j : list) {
							float cur_reply_height = j.second->get_height();
							if (cur_y + j.first < HIGH && cur_y + j.first + cur_reply_height > LOW) comments_list.push_back({cur_y + j.first, j.second});
						}
					}
					cur_y += cur_height;
				}
				if (comments_list.size() > MAX_THUMBNAIL_LOAD_REQUEST) {
					int leftover = comments_list.size() - MAX_THUMBNAIL_LOAD_REQUEST;
					comments_list.erase(comments_list.begin(), comments_list.begin() + leftover / 2);
					comments_list.erase(comments_list.end() - (leftover - leftover / 2), comments_list.end());
				}
			}
			
			std::set<CommentView *> newly_loading_views, cancelling_views;
			for (auto i : comment_thumbnail_loaded_list) cancelling_views.insert(i);
			for (auto i : comments_list) newly_loading_views.insert(i.second);
			for (auto i : comment_thumbnail_loaded_list) newly_loading_views.erase(i);
			for (auto i : comments_list) cancelling_views.erase(i.second);
			
			for (auto i : cancelling_views) {
				thumbnail_cancel_request(i->author_icon_handle);
				i->author_icon_handle = -1;
				comment_thumbnail_loaded_list.erase(i);
			}
			for (auto i : newly_loading_views) {
				i->author_icon_handle = thumbnail_request(i->get_yt_comment_object().author.icon_url, SceneType::VIDEO_PLAYER, 0, ThumbnailType::ICON);
				comment_thumbnail_loaded_list.insert(i);
			}
			
			std::vector<std::pair<int, int> > priority_list;
			auto priority = [&] (float i) { return 500 + (i < 0 ? i : 240 - i) / 100; };
			for (auto i : comments_list) priority_list.push_back({i.second->author_icon_handle, priority(i.first)});
			thumbnail_set_priorities(priority_list);
		}
		
		update_overlay_menu(&key, &intent, SceneType::VIDEO_PLAYER);
		if (selected_tab == TAB_COMMENTS) {
			comment_all_view->update(key);
			if (channel_url_pressed != "") {
				intent.next_scene = SceneType::CHANNEL;
				intent.arg = channel_url_pressed;
				channel_url_pressed = "";
			}
		}
		
		// tab selector
		{
			auto released_point = tab_selector_scroller.update(key, TAB_SELECTOR_HEIGHT);
			if (released_point.first != -1) {
				int next_tab = released_point.first * TAB_NUM / 320;
				scroller[next_tab].on_resume();
				selected_tab = next_tab;
				var_need_reflesh = true;
			}
		}
		// main scroller
		int content_height = 0;
		if (is_async_task_running(load_video_page)) content_height = DEFAULT_FONT_INTERVAL;
		else {
			if (selected_tab == TAB_GENERAL) {
				content_height += 15 * title_lines.size() + SMALL_MARGIN + DEFAULT_FONT_INTERVAL * 2;
				content_height += SMALL_MARGIN * 2;
				content_height += ICON_SIZE;
				if (cur_video_info.is_upcoming) content_height += 2 * SMALL_MARGIN + DEFAULT_FONT_INTERVAL;
				content_height += SMALL_MARGIN * 2;
				content_height += description_lines.size() * DEFAULT_FONT_INTERVAL;
				content_height += SMALL_MARGIN * 2;
			} else if (selected_tab == TAB_SUGGESTIONS) {
				content_height = cur_video_info.suggestions.size() * SUGGESTIONS_VERTICAL_INTERVAL;
				if (cur_video_info.has_more_suggestions() || cur_video_info.error != "") content_height += SUGGESTION_LOAD_MORE_MARGIN;
			} else if (selected_tab == TAB_COMMENTS) {
			} else if (selected_tab == TAB_ADVANCED) {
				content_height += DEFAULT_FONT_INTERVAL * 6;
				if (cur_video_info.is_livestream && network_decoder.ready) content_height += DEFAULT_FONT_INTERVAL;
				content_height += SMALL_MARGIN + DEFAULT_FONT_INTERVAL + SMALL_MARGIN; // texture filter button
				content_height += SMALL_MARGIN * 4;
				content_height += 160;
			}
		}
		auto released_point = scroller[selected_tab].update(key, content_height);
		int released_x = released_point.first;
		int released_y = released_point.second;
		if (selected_tab == TAB_GENERAL) {
			if (released_x != -1) {
				// TODO : move these to somewhere else
				/*
				if(released_x >= 165 && released_x <= 309 && released_y >= 165 && released_y <= 174)
				{
					vid_show_controls = !vid_show_controls;
					var_need_reflesh = true;
				}
				*/
				int cur_y = 0;
				cur_y += 15 * title_lines.size() + SMALL_MARGIN + DEFAULT_FONT_INTERVAL * 2;
				cur_y += SMALL_MARGIN * 2;
				if (released_x < ICON_SIZE + SMALL_MARGIN && released_y >= cur_y && released_y < cur_y + ICON_SIZE) {
					intent.next_scene = SceneType::CHANNEL;
					intent.arg = cur_video_info.author.url;
				}
			}
		} else if (selected_tab == TAB_SUGGESTIONS) {
			do {
				if (released_x != -1) {
					if (released_y < SUGGESTIONS_VERTICAL_INTERVAL * (int) cur_video_info.suggestions.size()) {
						int index = released_y / SUGGESTIONS_VERTICAL_INTERVAL;
						int remainder = released_y % SUGGESTIONS_VERTICAL_INTERVAL;
						if (remainder < SUGGESTION_THUMBNAIL_HEIGHT) {
							intent.next_scene = SceneType::VIDEO_PLAYER;
							intent.arg = cur_video_info.suggestions[index].url;
							break;
						}
					}
				}
				
				int load_more_y = SUGGESTIONS_VERTICAL_INTERVAL * cur_video_info.suggestions.size() - scroller[selected_tab].get_offset();
				if (cur_video_info.has_more_suggestions() && cur_video_info.error == "" && load_more_y < CONTENT_Y_HIGH &&
					!is_async_task_running(load_more_suggestions) && cur_video_info.suggestions.size()) {
						
					send_load_more_suggestions_request();
				}
			} while (0);
		} else if (selected_tab == TAB_COMMENTS) {
		} else if (selected_tab == TAB_ADVANCED) {
			int y_offset = 0;
			y_offset += DEFAULT_FONT_INTERVAL * 6;
			if (cur_video_info.is_livestream && network_decoder.ready) y_offset += DEFAULT_FONT_INTERVAL;
			y_offset += SMALL_MARGIN;
			if (released_x != -1) {
				// filter button
				if (released_x >= 15 && released_x < 150 && released_y >= y_offset && released_y < y_offset + DEFAULT_FONT_INTERVAL) {
					vid_linear_filter = !vid_linear_filter;
					for(int i = 0; i < 8; i++)
						Draw_c2d_image_set_filter(&vid_image[i], vid_linear_filter);

					var_need_reflesh = true;
				}
				// reload
				if (released_x >= 170 && released_x < 305 && released_y >= y_offset && released_y < y_offset + DEFAULT_FONT_INTERVAL) {
					if (!is_async_task_running(load_video_page)) {
						send_change_video_request(vid_url);
						video_retry_left = MAX_RETRY_CNT;
					}
				}
			}
			y_offset += DEFAULT_FONT_INTERVAL;
			y_offset += SMALL_MARGIN * 5;
			y_offset += 160;
		}
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		if (key.p_a) {
			if(vid_play_request) {
				if (vid_pausing) {
					if (!vid_pausing_seek) Util_speaker_resume(0);
					vid_pausing = false;
				} else {
					if (!vid_pausing_seek) Util_speaker_pause(0);
					vid_pausing = true;
				}
			} else if (cur_video_info.is_playable()) {
				network_waiting_status = NULL;
				vid_play_request = true;
			}

			var_need_reflesh = true;
		}
		/*
		TODO : reinstate this
		else if(key.p_b)
		{
			vid_play_request = false;
			var_need_reflesh = true;
		}*/
		else if(key.p_y)
		{
			vid_detail_mode = !vid_detail_mode;
			var_need_reflesh = true;
		}
		else if (key.p_b)
		{
			intent.next_scene = SceneType::BACK;
		}
		else if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		
		svcReleaseMutex(small_resource_lock);
		/* ****************************** LOCK END ******************************  */
		
		/*
		if(key.p_c_down || key.p_c_up || key.p_c_right || key.p_c_left || key.h_c_down || key.h_c_up || key.h_c_right || key.h_c_left
		|| key.p_d_down || key.p_d_up || key.p_d_right || key.p_d_left || key.h_d_down || key.h_d_up || key.h_d_right || key.h_d_left)
		{
			if(key.p_c_down || key.p_d_down)
				vid_y -= 1 * var_scroll_speed * key.count;
			else if(key.h_c_down || key.h_d_down)
			{
				if(vid_cd_count > 600)
					vid_y -= 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_y -= 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_y -= 5 * var_scroll_speed * key.count;
			}

			if(key.p_c_up || key.p_d_up)
				vid_y += 1 * var_scroll_speed * key.count;
			else if(key.h_c_up || key.h_d_up)
			{
				if(vid_cd_count > 600)
					vid_y += 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_y += 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_y += 5 * var_scroll_speed * key.count;
			}

			if(key.p_c_right || key.p_d_right)
				vid_x -= 1 * var_scroll_speed * key.count;
			else if(key.h_c_right || key.h_d_right)
			{
				if(vid_cd_count > 600)
					vid_x -= 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_x -= 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_x -= 5 * var_scroll_speed * key.count;
			}

			if(key.p_c_left || key.p_d_left)
				vid_x += 1 * var_scroll_speed * key.count;
			else if(key.h_c_left || key.h_d_left)
			{
				if(vid_cd_count > 600)
					vid_x += 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_x += 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_x += 5 * var_scroll_speed * key.count;
			}

			if(vid_x > 400)
				vid_x = 400;
			else if(vid_x < -vid_width * vid_zoom)
				vid_x = -vid_width * vid_zoom;

			if(vid_y > 480)
				vid_y = 480;
			else if(vid_y < -vid_height * vid_zoom)
				vid_y = -vid_height * vid_zoom;

			vid_cd_count++;
			var_need_reflesh = true;
		}
		else
			vid_cd_count = 0;

		if(key.p_l || key.p_r || key.h_l || key.h_r)
		{
			if(key.p_l)
				vid_zoom -= 0.005 * var_scroll_speed * key.count;
			else if(key.h_l)
			{
				if(vid_lr_count > 360)
					vid_zoom -= 0.05 * var_scroll_speed * key.count;
				else if(vid_lr_count > 120)
					vid_zoom -= 0.01 * var_scroll_speed * key.count;
				else if(vid_lr_count > 5)
					vid_zoom -= 0.005 * var_scroll_speed * key.count;
			}

			if(key.p_r)
				vid_zoom += 0.005 * var_scroll_speed * key.count;
			else if(key.h_r)
			{
				if(vid_lr_count > 360)
					vid_zoom += 0.05 * var_scroll_speed * key.count;
				else if(vid_lr_count > 120)
					vid_zoom += 0.01 * var_scroll_speed * key.count;
				else if(vid_lr_count > 5)
					vid_zoom += 0.005 * var_scroll_speed * key.count;
			}

			if(vid_zoom < 0.05)
				vid_zoom = 0.05;
			else if(vid_zoom > 10)
				vid_zoom = 10;

			vid_lr_count++;
			var_need_reflesh = true;
		}
		else
			vid_lr_count = 0;
		*/
		
		if ((key.h_x && key.p_y) || (key.h_y && key.p_x)) var_debug_mode = !var_debug_mode;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
