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
#include "system/util/misc_tasks.hpp"
#include "system/util/util.hpp"

#define ICON_SIZE 55
#define TAB_SELECTOR_HEIGHT 20
#define TAB_SELECTOR_SELECTED_LINE_HEIGHT 3
#define SUGGESTIONS_VERTICAL_INTERVAL (VIDEO_LIST_THUMBNAIL_HEIGHT + SMALL_MARGIN)
#define SUGGESTION_LOAD_MORE_MARGIN 30
#define COMMENT_LOAD_MORE_MARGIN 30
#define CONTROL_BUTTON_HEIGHT 20

#define MAX_THUMBNAIL_LOAD_REQUEST 20
#define MAX_RETRY_CNT 5

#define TAB_GENERAL 0
#define TAB_COMMENTS 1
#define TAB_SUGGESTIONS 2
#define TAB_CAPTIONS 3
#define TAB_PLAYBACK 4
#define TAB_PLAYLIST 5

#define TAB_MAX_NUM 6

namespace VideoPlayer {
	bool vid_main_run = false;
	bool vid_thread_run = false;
	bool vid_already_init = false;
	bool vid_thread_suspend = true;
	volatile bool vid_play_request = false;
	volatile bool vid_seek_request = false;
	volatile bool vid_change_video_request = false;
	volatile bool vid_pausing = false;
	volatile bool vid_pausing_seek = false;
	volatile bool eof_reached = false;
	volatile bool audio_only_mode = false;
	volatile bool video_skip_drawing = false; // for performance reason, enabled when opening keyboard
	volatile int video_p_value = 360;
	volatile double seek_at_init_request = -1;
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
	int vid_tex_width[2] = {0, 0};
	int vid_tex_height[2] = {0, 0};
	int vid_mvd_image_num = 0; // write the texture alternately to avoid scattering 
	std::string cur_displaying_url = "";
	std::string cur_playing_url = "";
	std::string vid_video_format = "n/a";
	std::string vid_audio_format = "n/a";
	Image_data vid_image[2];
	int icon_thumbnail_handle = -1;
	C2D_Image vid_banner[2];
	C2D_Image vid_control[2];
	C2D_Image play_button_texture[2];
	Thread vid_decode_thread, vid_convert_thread;
	
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
	YouTubeVideoDetail playing_video_info;
	std::map<std::string, YouTubeVideoDetail> video_info_cache;
	int video_retry_left = 0;
	
	std::set<PostView *> comment_thumbnail_loaded_list;
	
	volatile bool is_loading = false;
	std::string channel_url_pressed;
	std::string suggestion_clicked_url; // also used for playlist
	
	int TAB_NUM = 5;
	
	// main tab
	ScrollView *main_tab_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH));
	ImageView *main_icon_view = (new ImageView(0, 0, ICON_SIZE, ICON_SIZE));
	
	// suggestion tab
	VerticalListView *suggestion_main_view = (new VerticalListView(0, 0, 320))->set_margin(SMALL_MARGIN)->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST);
	View *suggestion_bottom_view = new EmptyView(0, 0, 320, 0);
	ScrollView *suggestion_view;
	
	// comment tab
	View *comments_top_view = new EmptyView(0, 0, 320, 4);
	VerticalListView *comments_main_view = new VerticalListView(0, 0, 320);
	View *comments_bottom_view = new EmptyView(0, 0, 320, 0);
	ScrollView *comment_all_view = NULL;
	
	// caption tab
	ScrollView *caption_main_view;
	VerticalListView *caption_language_select_view;
	View *captions_tab_view;
	CaptionOverlayView *caption_overlay_view;
	
	// list tab
	constexpr int PLAYLIST_TOP_HEIGHT = 30;
	VerticalListView *playlist_view;
	ScrollView *playlist_list_view;
	TextView *playlist_title_view;
	TextView *playlist_author_view;
	int playlist_thumbnail_request_l = 0;
	int playlist_thumbnail_request_r = 0;
	
	// playback tab
	SuccinctVideoView *cur_playing_video_view;
	CustomView *download_progress_view = NULL;
	SelectorView *video_quality_selector_view;
	VerticalListView *debug_info_view = NULL;
	ScrollView *playback_tab_view = NULL;
};
using namespace VideoPlayer;

static const char * volatile network_waiting_status = NULL;
const char *get_network_waiting_status() {
	if (network_waiting_status) return network_waiting_status;
	return network_decoder.get_network_waiting_status();
}

static void send_seek_request_wo_lock(double pos);
static void send_change_video_request(std::string url, bool update_player, bool update_view, bool force_load);
static void send_change_video_request_wo_lock(std::string url, bool update_player, bool update_view, bool force_load);

static void load_video_page(void *);
static void load_more_comments(void *);
static void load_more_suggestions(void *);
static void load_more_replies(void *);
static void load_caption(void *);

static void decode_thread(void* arg);
static void convert_thread(void* arg);


void VideoPlayer_init(void) {
	Util_log_save(DEF_SAPP0_INIT_STR, "Initializing...");
	bool new_3ds = false;
	Result_with_string result;
	
	vid_thread_run = true;

	
	svcCreateMutex(&network_decoder_critical_lock, false);
	svcCreateMutex(&small_resource_lock, false);
	
	tab_selector_scroller = VerticalScroller(0, 320, CONTENT_Y_HIGH, CONTENT_Y_HIGH + TAB_SELECTOR_HEIGHT);
	suggestion_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH))->set_views({suggestion_main_view, suggestion_bottom_view});
	comment_all_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH))->set_views({comments_top_view, comments_main_view, comments_bottom_view});
	captions_tab_view = caption_language_select_view = new VerticalListView(0, 0, 320);
	caption_main_view = new ScrollView(0, 0, 320, CONTENT_Y_HIGH);
	caption_overlay_view = new CaptionOverlayView(0, 0, 400, 240);
	cur_playing_video_view = (new SuccinctVideoView(SMALL_MARGIN, 0, 320 - SMALL_MARGIN * 2, VIDEO_LIST_THUMBNAIL_HEIGHT));
	download_progress_view = (new CustomView(0, 0, 320, SMALL_MARGIN * 2))
		->set_draw([] (const CustomView &view) {
			int y = view.y0;
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
						Draw_texture(var_square_image[0], a << 24 | b << 16 | g << 8 | r, xl, y, xr - xl, SMALL_MARGIN);
					}
					float head_x = 5 + bar_len * cur_pos;
					Draw_texture(var_square_image[0], DEF_DRAW_RED, head_x - 1, y, SMALL_MARGIN, SMALL_MARGIN);
				}
				y += SMALL_MARGIN;
			}
		});
	video_quality_selector_view = (new SelectorView(0, 0, 320, 35))
		->set_texts({
			(std::function<std::string ()>) []() { return LOCALIZED(OFF); }
		}, 0)
		->set_title([](const SelectorView &view) { return LOCALIZED(VIDEO); });
	debug_info_view = (new VerticalListView(0, 0, 320))
		->set_views({
			(new TextView(SMALL_MARGIN, 0, 320, DEFAULT_FONT_INTERVAL * 8))->set_text_lines<std::function<std::string ()> >({
				[] () { return vid_video_format; },
				[] () { return vid_audio_format; },
				[] () {
					return std::to_string(vid_width_org) + "x" + std::to_string(vid_height_org) + "@" + std::to_string(vid_framerate).substr(0, 5) + "fps";
				},
				[] () {
					auto decoder_type = network_decoder.get_decoder_type();
					std::string decoder_type_str =
						decoder_type == NetworkMultipleDecoder::DecoderType::HW ? LOCALIZED(HW_DECODER) :
						decoder_type == NetworkMultipleDecoder::DecoderType::MT_SLICE ? LOCALIZED(MULTITHREAD_SLICE) :
						decoder_type == NetworkMultipleDecoder::DecoderType::MT_FRAME ? LOCALIZED(MULTITHREAD_FRAME) : LOCALIZED(SINGLE_THREAD);
					return LOCALIZED(DECODER_TYPE) + " : " + decoder_type_str;
				},
				[] () {
					const char *message = get_network_waiting_status();
					return LOCALIZED(WAITING_STATUS) + " : " + std::string(message ? message : "");
				},
				[] () {
					u32 cpu_limit;
					APT_GetAppCpuTimeLimit(&cpu_limit);
					return LOCALIZED(CPU_LIMIT) + " : " + std::to_string(cpu_limit) + "%";
				},
				[] () {
					return LOCALIZED(FORWARD_BUFFER) + " : " + (cur_video_info.is_livestream && network_decoder.ready ? std::to_string(network_decoder.get_forward_buffer()) : "N/A");
				},
				[] () {
					return LOCALIZED(RAW_FRAME_BUFFER) + " : " + std::to_string(network_decoder.get_raw_buffer_num()) + "/" + std::to_string(network_decoder.get_raw_buffer_num_max());
				}
			}),
			(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2)),
			(new CustomView(0, 0, 320, 160))->set_draw([] (const CustomView &view) {
				int y = view.y0;
				
				// decoding time graph
				for (int i = 0; i < 319; i++) {
					Draw_line(i, y + 90 - vid_time[1][i], DEF_DRAW_BLUE, i + 1, y + 90 - vid_time[1][i + 1], DEF_DRAW_BLUE, 1); //Thread 1
					Draw_line(i, y + 90 - vid_time[0][i], DEF_DRAW_RED, i + 1, y + 90 - vid_time[0][i + 1], DEF_DRAW_RED, 1); //Thread 0
				}

				Draw_line(0, y + 90, DEFAULT_TEXT_COLOR, 320, y + 90, DEFAULT_TEXT_COLOR, 2);
				Draw_line(0, y + 90 - vid_frametime, 0xFFFFFF00, 320, y + 90 - vid_frametime, 0xFFFFFF00, 2);
				if(vid_total_frames != 0 && vid_min_time != 0  && vid_recent_total_time != 0) {
					Draw("avg " + std::to_string(1000 / (vid_total_time / vid_total_frames)).substr(0, 5) + " min " + std::to_string(1000 / vid_max_time).substr(0, 5) 
					+  " max " + std::to_string(1000 / vid_min_time).substr(0, 5) + " recent avg " + std::to_string(1000 / (vid_recent_total_time / 90)).substr(0, 5) +  " fps",
					0, y + 90, 0.4, 0.4, DEFAULT_TEXT_COLOR);
				}

				Draw("Deadline : " + std::to_string(vid_frametime).substr(0, 5) + "ms", 0, y + 100, 0.4, 0.4, 0xFFFFFF00);
				Draw("Video decode : " + std::to_string(vid_video_time).substr(0, 5) + "ms", 0, y + 110, 0.4, 0.4, DEF_DRAW_RED);
				Draw("Audio decode : " + std::to_string(vid_audio_time).substr(0, 5) + "ms", 0, y + 120, 0.4, 0.4, DEF_DRAW_RED);
				//Draw("Data copy 0 : " + std::to_string(vid_copy_time[0]).substr(0, 5) + "ms", 160, 120, 0.4, 0.4, DEF_DRAW_BLUE);
				Draw("Color convert : " + std::to_string(vid_convert_time).substr(0, 5) + "ms", 160, y + 110, 0.4, 0.4, DEF_DRAW_BLUE);
				Draw("Data copy 1 : " + std::to_string(vid_copy_time[1]).substr(0, 5) + "ms", 160, y + 120, 0.4, 0.4, DEF_DRAW_BLUE);
				Draw("Thread 0 : " + std::to_string(vid_time[0][319]).substr(0, 6) + "ms", 0, y + 130, 0.5, 0.5, DEF_DRAW_RED);
				Draw("Thread 1 : " + std::to_string(vid_time[1][319]).substr(0, 6) + "ms", 160, y + 130, 0.5, 0.5, DEF_DRAW_BLUE);
				Draw("Zoom : x" + std::to_string(vid_zoom).substr(0, 5) + " X : " + std::to_string((int)vid_x) + " Y : " + std::to_string((int)vid_y), 0, y + 140, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			})
		});
	playback_tab_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH))
		->set_margin(SMALL_MARGIN)
		->set_views({
			(new EmptyView(0, 0, 320, 0)), // margin at the top(using ScrollView's auto margin)
			(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(CUR_PLAYING_VIDEO); }),
			cur_playing_video_view,
			(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(BUFFERING_PROGRESS); }),
			download_progress_view,
			(new TextView(SMALL_MARGIN * 2, 0, 100, CONTROL_BUTTON_HEIGHT))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(RELOAD); })
				->set_x_alignment(TextView::XAlign::CENTER)
				->set_get_background_color([] (const View &) {
					return is_async_task_running(load_video_page) ? DEF_DRAW_LIGHT_GRAY : DEF_DRAW_WEAK_AQUA;
				})
				->set_on_view_released([] (View &view) {
					if (!is_async_task_running(load_video_page)) {
						seek_at_init_request = vid_current_pos;
						send_change_video_request_wo_lock(cur_playing_url, true, false, true);
						video_retry_left = MAX_RETRY_CNT;
					}
				}),
			video_quality_selector_view,
			(new BarView(0, 0, 320, 40)) // preamp
				->set_values(std::log(0.25), std::log(4), 0) // exponential scale
				->set_title([] (const BarView &view) { return LOCALIZED(PREAMP) + " : " + std::to_string((int) std::round(std::exp(view.value) * 100)) + "%"; })
				->set_while_holding([] (BarView &view) {
					double volume = std::exp(view.value);
					if (volume >= 0.95 && volume <= 1.05) volume = 1.0, view.value = 0;
					// volume change is needs a reconstruction of the filter graph, so don't update volume too often
					static int cnt = 0;
					if (++cnt >= 15) cnt = 0, network_decoder.preamp_change_request = volume;
				})
				->set_on_release([] (BarView &view) {
					double volume = std::exp(view.value);
					if (volume >= 0.95 && volume <= 1.05) volume = 1.0, view.value = 0;
					network_decoder.preamp_change_request = volume;
				}),
			(new BarView(0, 0, 320, 40)) // speed
				->set_values(0.3, 1.5, 1.0)
				->set_title([] (const BarView &view) { return LOCALIZED(SPEED) + " : " + std::to_string((int) std::round(view.value * 100)) + "%"; })
				->set_while_holding([] (BarView &view) {
					if (view.value >= 0.97 && view.value <= 1.03) view.value = 1.0;
					static int cnt = 0;
					if (++cnt >= 15) cnt = 0, network_decoder.tempo_change_request = view.value;
				})
				->set_on_release([] (BarView &view) {
					if (view.value >= 0.97 && view.value <= 1.03) view.value = 1.0;
					network_decoder.tempo_change_request = view.value;
				}),
			(new BarView(0, 0, 320, 40)) // pitch
				->set_values(0.5, 2.0, 1.0)
				->set_title([] (const BarView &view) { return LOCALIZED(PITCH) + " : " + std::to_string((int) std::round(view.value * 100)) + "%"; })
				->set_while_holding([] (BarView &view) {
					if (view.value >= 0.97 && view.value <= 1.03) view.value = 1.0;
					static int cnt = 0;
					if (++cnt >= 15) cnt = 0, network_decoder.pitch_change_request = view.value;
				})
				->set_on_release([] (BarView &view) {
					if (view.value >= 0.97 && view.value <= 1.03) view.value = 1.0;
					network_decoder.pitch_change_request = view.value;
				}),
			(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN)),
			debug_info_view
		});
	playlist_title_view = (new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL);
	playlist_author_view = (new TextView(0, 0, 320, PLAYLIST_TOP_HEIGHT - MIDDLE_FONT_INTERVAL))->set_get_text_color([] () { return LIGHT0_TEXT_COLOR; });
	playlist_list_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH - PLAYLIST_TOP_HEIGHT))->set_margin(SMALL_MARGIN);
	playlist_view = (new VerticalListView(0, 0, 320))->set_views({
		playlist_title_view,
		playlist_author_view,
		(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN)),
		playlist_list_view
	})->set_draw_order({3, 2, 1, 0});
	for (int i = 0; i < 3; i++) playlist_view->views[i]->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; });
	
	add_cpu_limit(CPU_LIMIT);
	if (var_is_new3ds) {
		bool frame_cores[4] = {false, true, var_core2_available, var_core3_available};
		bool slice_cores[4] = {false, true, false, var_core3_available};
		network_decoder.set_frame_cores_enabled(frame_cores);
		network_decoder.set_slice_cores_enabled(slice_cores);
		
		vid_decode_thread = threadCreate(decode_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 2, false);
		vid_convert_thread = threadCreate(convert_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
		livestream_initer_thread = threadCreate(livestream_initer_thread_func, &network_decoder, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 2, false);
	} else {
		bool frame_cores[4] = {true, true, false, false};
		bool slice_cores[4] = {false, true, false, false};
		network_decoder.set_frame_cores_enabled(frame_cores);
		network_decoder.set_slice_cores_enabled(slice_cores);
		
		vid_decode_thread = threadCreate(decode_thread, (void*)("1"), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 1, false);
		vid_convert_thread = threadCreate(convert_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
		livestream_initer_thread = threadCreate(livestream_initer_thread_func, &network_decoder, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 1, false);
	}
	stream_downloader = NetworkStreamDownloader();
	stream_downloader_thread = threadCreate(network_downloader_thread, &stream_downloader, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	livestream_initer_thread = threadCreate(livestream_initer_thread_func, &network_decoder, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 2, false);

	vid_total_time = 0;
	vid_total_frames = 0;
	vid_min_time = 99999999;
	vid_max_time = 0;
	vid_recent_total_time = 0;
	for(int i = 0; i < 90; i++)
		vid_recent_time[i] = 0;

	for(int i = 0; i < 2; i++)
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

	for(int i = 0 ; i < 2; i++)
	{
		result = Draw_c2d_image_init(&vid_image[i], 1024, 1024, GPU_RGB565);
		if(result.code != 0)
		{
			Util_err_set_error_message(DEF_ERR_OUT_OF_LINEAR_MEMORY_STR, "", DEF_SAPP0_INIT_STR, DEF_ERR_OUT_OF_LINEAR_MEMORY);
			Util_err_set_error_show_flag(true);
			vid_thread_run = false;
		}
		Draw_c2d_image_set_filter(&vid_image[i], var_video_linear_filter);
	}

	result = Draw_load_texture("romfs:/gfx/draw/video_player/banner.t3x", 61, vid_banner, 0, 2);
	if (result.code != 0) Util_log_save(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);

	result = Draw_load_texture("romfs:/gfx/draw/video_player/control.t3x", 62, vid_control, 0, 2);
	if (result.code != 0) Util_log_save(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);
	
	result = Draw_load_texture("romfs:/gfx/draw/thumb_up.t3x", 63, var_texture_thumb_up, 0, 2);
	if (result.code != 0) Util_log_save(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);
	
	result = Draw_load_texture("romfs:/gfx/draw/thumb_down.t3x", 64, var_texture_thumb_down, 0, 2);
	if (result.code != 0) Util_log_save(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);
	
	
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
	video_p_value = var_is_new3ds ? 360 : 144;
	
	VideoPlayer_resume("");
	vid_already_init = true;
	
	
	video_set_show_debug_info(var_video_show_debug_info);
	video_set_linear_filter_enabled(var_video_linear_filter);
	Util_log_save(DEF_SAPP0_INIT_STR, "Initialized.");
}
void VideoPlayer_exit(void) {
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
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	
	// clean up views
	suggestion_view->recursive_delete_subviews();
	delete suggestion_view;
	suggestion_view = NULL;
	suggestion_main_view = NULL;
	suggestion_bottom_view = NULL;
	
	comment_all_view->recursive_delete_subviews();
	delete comment_all_view;
	comments_top_view = NULL;
	comments_main_view = NULL;
	comments_bottom_view = NULL;
	comment_all_view = NULL;
	
	delete caption_language_select_view;
	delete caption_main_view;
	delete caption_overlay_view;
	caption_language_select_view = NULL;
	caption_main_view = NULL;
	caption_overlay_view = NULL;
	captions_tab_view = NULL;
	
	playback_tab_view->recursive_delete_subviews();
	delete playback_tab_view;
	playback_tab_view = NULL;
	debug_info_view = NULL;
	download_progress_view = NULL;
	
	playlist_view->recursive_delete_subviews();
	delete playlist_view;
	playlist_view = NULL;
	
	svcReleaseMutex(small_resource_lock);
	
	remove_cpu_limit(CPU_LIMIT);

	Draw_free_texture(61);
	Draw_free_texture(62);
	Draw_free_texture(63);
	Draw_free_texture(64);

	for(int i = 0; i < 2; i++)
		Draw_c2d_image_free(vid_image[i]);
	
	Util_log_save(DEF_SAPP0_EXIT_STR, "Exited.");
}
void VideoPlayer_suspend(void) {
	vid_thread_suspend = true;
	vid_main_run = false;
}
void VideoPlayer_resume(std::string arg) {
	if (arg != "") {
		if (arg != cur_displaying_url) {
			send_change_video_request(arg, !vid_play_request || (vid_pausing && eof_reached), true, false);
			video_retry_left = MAX_RETRY_CNT;
		}
	}
	main_tab_view->reset();
	suggestion_view->reset();
	comment_all_view->reset();
	caption_main_view->reset();
	playlist_list_view->reset();
	
	
	overlay_menu_on_resume();
	vid_thread_suspend = false;
	vid_main_run = true;
	var_need_reflesh = true;
}



// START : functions called from async_task.cpp
/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */

#define TITLE_MAX_WIDTH (320 - SMALL_MARGIN * 2)
#define DESC_MAX_WIDTH (320 - SMALL_MARGIN * 2)
#define SUGGESTION_TITLE_MAX_WIDTH (320 - (VIDEO_LIST_THUMBNAIL_WIDTH + SMALL_MARGIN))
#define COMMENT_MAX_WIDTH (320 - (POST_ICON_SIZE + 2 * SMALL_MARGIN))
#define REPLY_INDENT 25
#define REPLY_MAX_WIDTH (320 - REPLY_INDENT - (REPLY_ICON_SIZE + 2 * SMALL_MARGIN))
#define CAPTION_TIMESTAMP_WIDTH 60
#define CAPTION_CONTENT_MAX_WIDTH (320 - CAPTION_TIMESTAMP_WIDTH)


static void update_suggestion_bottom_view() {
	delete suggestion_bottom_view;
	if (cur_video_info.error != "" || cur_video_info.has_more_suggestions() || !cur_video_info.suggestions.size()) {
		TextView *bottom_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))
			->set_text(
				cur_video_info.error != "" ? cur_video_info.error :
				cur_video_info.has_more_suggestions() ? LOCALIZED(LOADING) :
				!cur_video_info.suggestions.size() ? LOCALIZED(EMPTY) : "IE")
			->set_font_size(0.5, DEFAULT_FONT_INTERVAL)
			->set_x_alignment(TextView::XAlign::CENTER)
			->set_y_alignment(TextView::YAlign::UP);
		
		suggestion_view->set_on_child_drawn(1, [] (const ScrollView &, int) {
			if (cur_video_info.has_more_suggestions() && cur_video_info.error == "") {
				if (!is_async_task_running(load_video_page) &&
					!is_async_task_running(load_more_suggestions)) queue_async_task(load_more_suggestions, &cur_video_info);
			}
		});
		suggestion_bottom_view = bottom_view;
	} else suggestion_bottom_view = new EmptyView(0, 0, 320, 0);
	suggestion_view->views[1] = suggestion_bottom_view;
}
// also used for playlist items
static SuccinctVideoView *suggestion_to_view(const YouTubeSuccinctItem &item) {
	SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT));
	cur_view->set_title_lines(truncate_str(item.get_name(), SUGGESTION_TITLE_MAX_WIDTH, 2, 0.5, 0.5));
	cur_view->set_thumbnail_url(item.get_thumbnail_url());
	if (item.type == YouTubeSuccinctItem::VIDEO) {
		cur_view->set_auxiliary_lines({item.video.author});
		cur_view->set_bottom_right_overlay(item.video.duration_text);
	} else if (item.type == YouTubeSuccinctItem::PLAYLIST) {
		cur_view->set_auxiliary_lines({item.playlist.video_count_str});
	}
	cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
	cur_view->set_on_view_released([item] (View &view) {
		suggestion_clicked_url = item.get_url();
	});
	cur_view->set_is_playlist(item.type == YouTubeSuccinctItem::PLAYLIST);
	
	return cur_view;
}

static void update_comment_bottom_view() {
	delete comments_bottom_view;
	if (cur_video_info.comments_disabled || cur_video_info.error != "" || cur_video_info.has_more_comments() || !cur_video_info.comments.size()) {
		TextView *bottom_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))
			->set_text(
				cur_video_info.comments_disabled ? LOCALIZED(COMMENTS_DISABLED) :
				cur_video_info.error != "" ? cur_video_info.error :
				cur_video_info.has_more_comments() ? LOCALIZED(LOADING) :
				!cur_video_info.comments.size() ? LOCALIZED(NO_COMMENTS) : "IE")
			->set_font_size(0.5, DEFAULT_FONT_INTERVAL)
			->set_x_alignment(TextView::XAlign::CENTER)
			->set_y_alignment(TextView::YAlign::UP);
		
		comment_all_view->set_on_child_drawn(2, [] (const ScrollView &, int) {
			if (cur_video_info.has_more_comments() && cur_video_info.error == "") {
				if (!is_async_task_running(load_video_page) &&
					!is_async_task_running(load_more_comments)) queue_async_task(load_more_comments, &cur_video_info);
			}
		});
		comments_bottom_view = bottom_view;
	} else comments_bottom_view = new EmptyView(0, 0, 320, 0);
	comment_all_view->views[2] = comments_bottom_view;
}
#define COMMENT_MAX_LINE_NUM 1000 // this limit exists due to performance reason (TODO : more efficient truncating)
static PostView *comment_to_view(const YouTubeVideoDetail::Comment &comment, int comment_index) {
	auto &cur_content = comment.content;
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
	std::string author_url = comment.author.url;
	return (new PostView(0, 0, 320))
		->set_author_name(comment.author.name)
		->set_author_icon_url(comment.author.icon_url)
		->set_time_str(comment.publish_date)
		->set_upvote_str(comment.upvotes_str)
		->set_content_lines(cur_lines)
		->set_has_more_replies([comment_index] () { return cur_video_info.comments[comment_index].has_more_replies(); })
		->set_on_author_icon_pressed([author_url] (const PostView &view) { channel_url_pressed = author_url; })
		->set_on_load_more_replies_pressed([comment_index] (PostView &view) {
			queue_async_task(load_more_replies, (void *) comment_index);
			view.is_loading_replies = true;
		});
}

// arg : 
//   cur_playing_url : only update the data for the player
//   cur_displaying_url : only update the displayed data
//   NULL : both
static void load_video_page(void *arg) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	bool is_to_play = ((const std::string *) arg) == &cur_playing_url;
	bool is_to_display = ((const std::string *) arg) == &cur_displaying_url;
	std::string url;
	if (!arg) {
		is_to_play = is_to_display = true;
		url = cur_playing_url;
	} else url = *((const std::string *) arg);
	YouTubeVideoDetail tmp_video_info;
	bool need_loading = false;
	if (video_info_cache.count(url)) tmp_video_info = video_info_cache[url];
	else need_loading = true;
	if (need_loading && is_to_display) is_loading = true;
	svcReleaseMutex(small_resource_lock);
	
	if (need_loading) {
		Util_log_save("player/load-v", "request : " + url);
		add_cpu_limit(ADDITIONAL_CPU_LIMIT);
		tmp_video_info = youtube_parse_video_page(url);
		remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	}
	
	if (is_to_display) {
		Util_log_save("player/load-v", "truncate/view creation start");
		// prepare views in the main tab
		std::vector<View *> main_tab_views;
		ImageView *new_main_icon_view = (new ImageView(0, 0, ICON_SIZE, ICON_SIZE));
		if (tmp_video_info.title == "" && tmp_video_info.playability_reason != "") {
			main_tab_views = {
				(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2.5))
					->set_text_lines(truncate_str(tmp_video_info.playability_reason, 320 - SMALL_MARGIN * 2, 3, 0.5, 0.5))
					->set_x_alignment(TextView::XAlign::CENTER)
			};
		} else {
			// wrap main title
			float title_font_size;
			std::vector<std::string> title_lines = truncate_str(tmp_video_info.title, TITLE_MAX_WIDTH, 3, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			if (title_lines.size() == 3) {
				title_lines = truncate_str(tmp_video_info.title, TITLE_MAX_WIDTH, 2, 0.5, 0.5);
				title_font_size = 0.5;
			} else title_font_size = MIDDLE_FONT_SIZE;
			
			std::string author_url = tmp_video_info.author.url;
			new_main_icon_view
				->set_handle(thumbnail_request(tmp_video_info.author.icon_url, SceneType::VIDEO_PLAYER, 1000, ThumbnailType::ICON))
				->set_on_view_released([author_url] (View &) { channel_url_pressed = author_url; });
			main_tab_views = {
				(new TextView(0, 0, 320, 15 * title_lines.size())) // title
					->set_text_lines(title_lines)
					->set_font_size(title_font_size, 15),
				(new EmptyView(0, 0, 320, SMALL_MARGIN)),
				(new HorizontalListView(0, 0, DEFAULT_FONT_INTERVAL)) // view count / publish date
					->set_views({
						(new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL))
							->set_text(tmp_video_info.views_str)
							->set_get_text_color([] () { return LIGHT0_TEXT_COLOR; }),
						(new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL))
							->set_text(tmp_video_info.publish_date)
							->set_x_alignment(TextView::XAlign::RIGHT)
							->set_get_text_color([] () { return LIGHT0_TEXT_COLOR; })
					}),
				(new CustomView(0, 0, 320, DEFAULT_FONT_INTERVAL)) // like/dislike
					->set_draw([] (const CustomView &view) {
						int y = view.y0;
						float x = view.x0 + SMALL_MARGIN;
						Draw_texture(var_texture_thumb_up[var_night_mode], x, y, 16, 16);
						x += 16 + SMALL_MARGIN;
						Draw(cur_video_info.like_count_str, x, y, 0.5, 0.5, LIGHT0_TEXT_COLOR);
						x += Draw_get_width(cur_video_info.like_count_str, 0.5) + SMALL_MARGIN * 2;
						x = (int) x;
						Draw_texture(var_texture_thumb_down[var_night_mode], x, y, 16, 16);
						x += 16 + SMALL_MARGIN;
						Draw(cur_video_info.dislike_count_str, x, y, 0.5, 0.5, LIGHT0_TEXT_COLOR);
					}),
				(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
					->set_get_color([] () { return DEF_DRAW_GRAY; }),
				(new HorizontalListView(0, 0, ICON_SIZE)) // author
					->set_views({
						(new EmptyView(0, 0, SMALL_MARGIN, ICON_SIZE)),
						new_main_icon_view,
						(new EmptyView(0, 0, SMALL_MARGIN, ICON_SIZE)),
						(new VerticalListView(0, 0, 320 - SMALL_MARGIN - ICON_SIZE))
							->set_views({
								(new EmptyView(0, 0, 320 - SMALL_MARGIN - ICON_SIZE, ICON_SIZE * 0.1)),
								(new TextView(0, 0, 320 - SMALL_MARGIN - ICON_SIZE, DEFAULT_FONT_INTERVAL + SMALL_MARGIN))
									->set_text(tmp_video_info.author.name)
									->set_font_size(0.55, DEFAULT_FONT_INTERVAL + SMALL_MARGIN),
								(new TextView(0, 0, 320 - SMALL_MARGIN - ICON_SIZE, DEFAULT_FONT_INTERVAL))
									->set_text(tmp_video_info.author.subscribers)
									->set_font_size(0.45, DEFAULT_FONT_INTERVAL + SMALL_MARGIN)
									->set_get_text_color([] () { return LIGHT1_TEXT_COLOR; })
							})
					}),
				(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
					->set_get_color([] () { return DEF_DRAW_GRAY; })
			};
			if (tmp_video_info.is_upcoming) {
				std::vector<View *> add_views = {
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
						->set_text(tmp_video_info.playability_reason),
					(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
						->set_get_color([] () { return DEF_DRAW_GRAY; })
				};
				main_tab_views.insert(main_tab_views.end(), add_views.begin(), add_views.end());
			}
			{
				TextView *play_button = (new TextView(0, 0, 160 - SMALL_MARGIN, 20));
				play_button
					->set_text((std::function<std::string ()>) [] () { return cur_video_info.id == playing_video_info.id ? LOCALIZED(PLAYING) : LOCALIZED(PLAY); })
					->set_x_alignment(TextView::XAlign::CENTER);
				if (tmp_video_info.is_playable()) {
					play_button
						->set_on_view_released([url] (View &) {
							if (cur_video_info.id != playing_video_info.id) send_change_video_request_wo_lock(url, true, false, false);
						})
						->set_get_background_color([] (const View &view) -> u32 {
							if (cur_video_info.id == playing_video_info.id) return 0xFFFFBBBB;
							return View::STANDARD_BACKGROUND(view);
						});
				} else play_button->set_get_background_color([] (const View &) { return LIGHT1_BACK_COLOR; });
				
				TextView *reload_button = (new TextView(0, 0, 160 - SMALL_MARGIN, 20));
				reload_button
					->set_text((std::function<std::string ()>) [] () { return LOCALIZED(RELOAD); })
					->set_x_alignment(TextView::XAlign::CENTER)
					->set_get_background_color(View::STANDARD_BACKGROUND)
					->set_on_view_released([url] (View &) {
						if (!is_async_task_running(load_video_page)) {
							send_change_video_request_wo_lock(cur_displaying_url, false, true, true);
							video_retry_left = MAX_RETRY_CNT;
						}
					});
				
				
				main_tab_views.push_back(
					(new HorizontalListView(0, 0, 20))
						->set_views({
							(new EmptyView(0, 0, SMALL_MARGIN, 20)),
							play_button,
							reload_button
						})
				);
				main_tab_views.push_back(
					(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
						->set_get_color([] () { return DEF_DRAW_GRAY; })
				);
			}
			{
				std::vector<std::string> description_lines;
				auto &description = tmp_video_info.description;
				auto itr = description.begin();
				while (itr != description.end()) {
					auto next_itr = std::find(itr, description.end(), '\n');
					auto cur_lines = truncate_str(std::string(itr, next_itr), DESC_MAX_WIDTH, 100, 0.5, 0.5);
					description_lines.insert(description_lines.end(), cur_lines.begin(), cur_lines.end());
					if (next_itr != description.end()) itr = std::next(next_itr);
					else break;
				}
				std::vector<View *> add_views = {
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * description_lines.size()))
						->set_text_lines(description_lines),
					(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
						->set_get_color([] () { return DEF_DRAW_GRAY; })
				};
				main_tab_views.insert(main_tab_views.end(), add_views.begin(), add_views.end());
			}
		}
		// prepare suggestion view
		std::vector<View *> new_suggestion_views;
		for (size_t i = 0; i < tmp_video_info.suggestions.size(); i++) new_suggestion_views.push_back(suggestion_to_view(tmp_video_info.suggestions[i]));
		
		// prepare comment views (comments exist from the first if it's loaded from cache)
		std::vector<View *> new_comment_views;
		for (size_t i = 0; i < tmp_video_info.comments.size(); i++) new_comment_views.push_back(comment_to_view(tmp_video_info.comments[i], i));
		
		// prepare captions view
		static std::string selected_base_lang = "";
		static std::string selected_translation_lang = "";
		static std::pair<std::string *, std::string *> load_caption_arg = {&selected_base_lang, &selected_translation_lang};
		
		selected_base_lang = tmp_video_info.caption_base_languages.size() ? tmp_video_info.caption_base_languages[0].id : "";
		selected_translation_lang = "";
		
		int exit_button_height = DEFAULT_FONT_INTERVAL * 1.5;
		View *exit_button_view = (new HorizontalListView(0, 0, exit_button_height))->set_views({
			(new EmptyView(0, 0, 320 - 100, exit_button_height))
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			(new TextView(0, 0, 100, exit_button_height))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(OK); })
				->set_x_alignment(TextView::XAlign::CENTER)
				->set_text_offset(0, -2.0)
				->set_background_color(COLOR_GRAY(0x80))
				->set_on_view_released([] (View &view) { queue_async_task(load_caption, &load_caption_arg); })
			}
		)->set_draw_order({1, 0});
		
		int language_selector_height = CONTENT_Y_HIGH - exit_button_view->get_height();
		// base languages
		VerticalListView *caption_left_view = new VerticalListView(0, 0, 160);
		caption_left_view->views.push_back((new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL * 1.2))
			->set_text((std::function<std::string ()>) [] () { return LOCALIZED(CAPTION_BASE_LANGUAGES); })
			->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; })
		);
		caption_left_view->views.push_back((new HorizontalRuleView(0, 0, 160, 3))
			->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }));
		ScrollView *base_languages_selector_view = new ScrollView(0, 0, 160, language_selector_height - caption_left_view->get_height());
		for (size_t i = 0; i < tmp_video_info.caption_base_languages.size(); i++) {
			auto &cur_lang = tmp_video_info.caption_base_languages[i];
			base_languages_selector_view->views.push_back((new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL))
				->set_text(cur_lang.name)
				->set_text_offset(0.0, -1.0)
				->set_get_background_color([cur_lang] (const View &view) {
					if (cur_lang.id == selected_base_lang) return COLOR_GRAY(0x80);
					else {
						int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - std::min(1.0, 0.15 * view.view_holding_time)));
						if (var_night_mode) darkness = 0xFF - darkness;
						return COLOR_GRAY(darkness);
					}
				})
				->set_on_view_released([cur_lang] (View &view) { selected_base_lang = cur_lang.id; })
			);
		}
		caption_left_view->views.push_back(base_languages_selector_view);
		caption_left_view->set_draw_order({2, 0, 1});
		
		// translation languages
		VerticalListView *caption_right_view = new VerticalListView(0, 0, 160);
		ScrollView *translation_languages_selector_view = new ScrollView(0, 0, 160, 0); // dummy height
		caption_right_view->views.push_back((new SelectorView(0, 0, 160, DEFAULT_FONT_INTERVAL * 2.5))
			->set_texts({
				(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
				(std::function<std::string ()>) []() { return LOCALIZED(ON); }
			}, 0)
			->set_title([](const SelectorView &) { return LOCALIZED(CAPTION_TRANSLATION); })
			->set_on_change([translation_languages_selector_view](const SelectorView &view) {
				translation_languages_selector_view->set_is_visible(view.selected_button)->set_is_touchable(view.selected_button);
			})
			->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; })
		);
		translation_languages_selector_view->set_height(language_selector_height - caption_right_view->get_height());
		for (size_t i = 0; i < tmp_video_info.caption_translation_languages.size(); i++) {
			auto &cur_lang = tmp_video_info.caption_translation_languages[i];
			TextView *cur_option_view = (new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL))
				->set_text(cur_lang.name)
				->set_text_offset(0.0, -1.0);
			cur_option_view->set_get_background_color([cur_lang] (const View &view) {
				if (cur_lang.id == selected_translation_lang) return COLOR_GRAY(0x80);
				else {
					int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - std::min(1.0, 0.15 * view.view_holding_time)));
					if (var_night_mode) darkness = 0xFF - darkness;
					return COLOR_GRAY(darkness);
				}
			});
			cur_option_view->set_on_view_released([cur_lang] (View &view) { selected_translation_lang = cur_lang.id; });
			translation_languages_selector_view->views.push_back(cur_option_view);
		}
		translation_languages_selector_view->set_is_visible(false)->set_is_touchable(false);
		caption_right_view->views.push_back(translation_languages_selector_view);
		caption_right_view->set_draw_order({1, 0});
		
		
		// prepare playlist view
		double playlist_view_scroll = playlist_list_view->get_offset(); // this call should not need mutex locking
		std::vector<View *> new_playlist_views;
		for (auto video : tmp_video_info.playlist.videos) new_playlist_views.push_back(suggestion_to_view(YouTubeSuccinctItem(video)));
		if (tmp_video_info.playlist.id != "" && tmp_video_info.playlist.id == playing_video_info.playlist.id) {
			for (size_t i = 0; i < tmp_video_info.playlist.videos.size(); i++) {
				if (youtube_get_video_id_by_url(tmp_video_info.playlist.videos[i].url) == playing_video_info.id) {
					new_playlist_views[i]->set_get_background_color([] (const View &) { return COLOR_LIGHT_BLUE; });
				}
			}
		}
		if (tmp_video_info.playlist.selected_index >= 0 && tmp_video_info.playlist.selected_index < (int) new_playlist_views.size()) {
			new_playlist_views[tmp_video_info.playlist.selected_index]->set_get_background_color([] (const View &) { return COLOR_LIGHT_GREEN; });
			double selected_y = tmp_video_info.playlist.selected_index * SUGGESTIONS_VERTICAL_INTERVAL;
			if (playlist_view_scroll < selected_y - (CONTENT_Y_HIGH - PLAYLIST_TOP_HEIGHT))
				playlist_view_scroll = selected_y - (CONTENT_Y_HIGH - PLAYLIST_TOP_HEIGHT) + SUGGESTIONS_VERTICAL_INTERVAL;
			if (playlist_view_scroll > selected_y + SUGGESTIONS_VERTICAL_INTERVAL) playlist_view_scroll = selected_y;
		} else playlist_view_scroll = 0;
		playlist_view_scroll = std::min<double>(playlist_view_scroll, tmp_video_info.playlist.videos.size() * SUGGESTIONS_VERTICAL_INTERVAL - (CONTENT_Y_HIGH - PLAYLIST_TOP_HEIGHT));
		playlist_view_scroll = std::max<double>(playlist_view_scroll, 0);
		
		Util_log_save("player/load-v", "truncate/view creation end");
		
		
		// acquire lock and perform actual replacements
		svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
		if (!vid_already_init) { // app shut down while loading
			svcReleaseMutex(small_resource_lock);
			return;
		}
		cur_video_info = tmp_video_info;
		video_info_cache[url] = tmp_video_info;
		TAB_NUM = 5;
		if (cur_video_info.playlist.videos.size()) TAB_NUM++, selected_tab = TAB_PLAYLIST;
		else if (selected_tab == TAB_PLAYLIST) selected_tab = TAB_GENERAL;
		
		thumbnail_cancel_request(main_icon_view->handle);
		main_tab_view->recursive_delete_subviews();
		main_icon_view = new_main_icon_view;
		main_tab_view->set_views({main_tab_views});
		
		suggestion_main_view->recursive_delete_subviews();
		suggestion_main_view->views = new_suggestion_views;
		suggestion_view->reset();
		update_suggestion_bottom_view();
		
		// cancel thumbnail requests
		for (auto view : comment_thumbnail_loaded_list) {
			thumbnail_cancel_request(view->author_icon_handle);
			view->author_icon_handle = -1;
		}
		comment_thumbnail_loaded_list.clear();
		comments_main_view->recursive_delete_subviews();
		comments_main_view->views = new_comment_views;
		comment_all_view->reset();
		update_comment_bottom_view();
		
		caption_main_view->recursive_delete_subviews();
		caption_overlay_view->set_caption_data({});
		caption_language_select_view->recursive_delete_subviews();
		caption_language_select_view->set_views({exit_button_view,
			(new HorizontalListView(0, 0, CONTENT_Y_HIGH - exit_button_view->get_height()))->set_views({caption_left_view, caption_right_view})});
		caption_language_select_view->set_draw_order({1, 0});
		captions_tab_view = caption_language_select_view;
		
		playlist_title_view->set_text(tmp_video_info.playlist.title);
		playlist_author_view->set_text(tmp_video_info.playlist.author_name);
		for (auto view : playlist_list_view->views) thumbnail_cancel_request(dynamic_cast<SuccinctVideoView *>(view)->thumbnail_handle);
		playlist_list_view->recursive_delete_subviews();
		playlist_list_view->views = new_playlist_views;
		playlist_list_view->set_offset(playlist_view_scroll);
		playlist_thumbnail_request_l = playlist_thumbnail_request_r = 0;
		
		is_loading = false;
		var_need_reflesh = true;
		svcReleaseMutex(small_resource_lock);
	}
	
	if (is_to_play && tmp_video_info.is_playable()) {
		svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
		if (!vid_already_init) { // app shut down while loading
			svcReleaseMutex(small_resource_lock);
			return;
		}
		playing_video_info = tmp_video_info;
		video_info_cache[url] = tmp_video_info;
		
		vid_change_video_request = true;
		if (network_decoder.ready) network_decoder.interrupt = true;
		
		thumbnail_cancel_request(cur_playing_video_view->thumbnail_handle);
		cur_playing_video_view->thumbnail_handle = -1;
		cur_playing_video_view
			->set_title_lines(truncate_str(tmp_video_info.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - SMALL_MARGIN * 3, 2, 0.5, 0.5))
			->set_thumbnail_url(tmp_video_info.succinct_thumbnail_url)
			->set_auxiliary_lines({tmp_video_info.author.name});
		cur_playing_video_view->thumbnail_handle = thumbnail_request(tmp_video_info.succinct_thumbnail_url, SceneType::VIDEO_PLAYER, 999, ThumbnailType::VIDEO_THUMBNAIL);
		
		// prepare the quality selector
		std::vector<int> available_qualities;
		for (auto &i : tmp_video_info.video_stream_urls) available_qualities.push_back(i.first);
		if (!std::count(available_qualities.begin(), available_qualities.end(), 360))
			available_qualities.insert(std::lower_bound(available_qualities.begin(), available_qualities.end(), 360), 360);
		
		video_quality_selector_view->button_texts = { (std::function<std::string ()>) []() { return LOCALIZED(OFF); } };
		for (auto i : available_qualities) if (var_is_new3ds || i <= 240) video_quality_selector_view->button_texts.push_back(std::to_string(i) + "p");
		video_quality_selector_view->button_num = video_quality_selector_view->button_texts.size();
		
		if (!audio_only_mode && !tmp_video_info.video_stream_urls.count((int) video_p_value)) {
			video_p_value = var_is_new3ds ? 360 : 144;
			if (!tmp_video_info.video_stream_urls.count((int) video_p_value)) audio_only_mode = true;
		}
		video_quality_selector_view->selected_button = audio_only_mode ? 0 : 1 + std::find(available_qualities.begin(), available_qualities.end(), (int) video_p_value) - available_qualities.begin();
		video_quality_selector_view->set_on_change([available_qualities] (const SelectorView &view) {
			bool changed = false;
			if (view.selected_button == 0) {
				if (!audio_only_mode) changed = true;
				audio_only_mode = true;
			} else {
				int new_p_value = available_qualities[view.selected_button - 1];
				if (audio_only_mode || video_p_value != new_p_value) changed = true;
				audio_only_mode = false;
				video_p_value = new_p_value;
			}
			if (changed) {
				seek_at_init_request = vid_current_pos;
				vid_change_video_request = true;
				if (network_decoder.ready) network_decoder.interrupt = true;
			}
		});
		// update playlist tab
		if (cur_video_info.playlist.id != "" && cur_video_info.playlist.id == tmp_video_info.playlist.id) {
			for (size_t i = 0; i < cur_video_info.playlist.videos.size(); i++) {
				if ((int) i == cur_video_info.playlist.selected_index)
					playlist_list_view->views[i]->set_get_background_color([] (const View &) { return COLOR_LIGHT_BLUE; });
				else if (youtube_get_video_id_by_url(tmp_video_info.playlist.videos[i].url) == tmp_video_info.id)
					playlist_list_view->views[i]->set_get_background_color([] (const View &) { return COLOR_LIGHT_GREEN; });
				else playlist_list_view->views[i]->set_get_background_color(View::STANDARD_BACKGROUND);
			}
		}
		svcReleaseMutex(small_resource_lock);
	}
}
static void load_more_suggestions(void *arg_) {
	YouTubeVideoDetail *arg = (YouTubeVideoDetail *) arg_;
	
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto new_result = youtube_video_page_load_more_suggestions(*arg);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	// wrap suggestion titles
	Util_log_save("player/load-s", "truncate/view creation start");
	std::vector<View *> new_suggestion_views;
	for (size_t i = arg->suggestions.size(); i < new_result.suggestions.size(); i++) new_suggestion_views.push_back(suggestion_to_view(new_result.suggestions[i]));
	Util_log_save("player/load-s", "truncate/view creation end");
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (!vid_already_init) { // app shut down while loading
		svcReleaseMutex(small_resource_lock);
		return;
	}
	if (new_result.error != "") cur_video_info.error = new_result.error;
	else {
		cur_video_info = new_result;
		suggestion_main_view->views.insert(suggestion_main_view->views.end(), new_suggestion_views.begin(), new_suggestion_views.end());
		update_suggestion_bottom_view();
		video_info_cache[cur_video_info.url] = new_result;
	}
	var_need_reflesh = true;
	svcReleaseMutex(small_resource_lock);
}

static void load_more_comments(void *arg_) {
	YouTubeVideoDetail *arg = (YouTubeVideoDetail *) arg_;
	
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto new_result = youtube_video_page_load_more_comments(*arg);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	std::vector<View *> new_comment_views;
	// wrap comments
	Util_log_save("player/load-c", "truncate/views creation start");
	for (size_t i = arg->comments.size(); i < new_result.comments.size(); i++) new_comment_views.push_back(comment_to_view(new_result.comments[i], i));
	Util_log_save("player/load-c", "truncate/views creation end");
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (!vid_already_init) { // app shut down while loading
		svcReleaseMutex(small_resource_lock);
		return;
	}
	cur_video_info = new_result;
	video_info_cache[cur_video_info.url] = new_result;
	comments_main_view->views.insert(comments_main_view->views.end(), new_comment_views.begin(), new_comment_views.end());
	update_comment_bottom_view();
	var_need_reflesh = true;
	svcReleaseMutex(small_resource_lock);
}

static void load_more_replies(void *arg_) {
	int comment_index = (int) arg_;
	PostView *comment_view = dynamic_cast<PostView *>(comments_main_view->views[comment_index]);
	YouTubeVideoDetail::Comment &comment = cur_video_info.comments[comment_index];
	
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto new_comment = youtube_video_page_load_more_replies(comment);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	std::vector<PostView *> new_reply_views;
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
		std::string author_url = cur_reply.author.url;
		new_reply_views.push_back((new PostView(REPLY_INDENT, 0, 320 - REPLY_INDENT))
			->set_author_name(cur_reply.author.name)
			->set_author_icon_url(cur_reply.author.icon_url)
			->set_time_str(cur_reply.publish_date)
			->set_upvote_str(cur_reply.upvotes_str)
			->set_content_lines(cur_lines)
			->set_has_more_replies([] () { return false; })
			->set_on_author_icon_pressed([author_url] (const PostView &view) { channel_url_pressed = author_url; })
			->set_is_reply(true)
		);
	}
	Util_log_save("player/load-r", "truncate end");
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (!vid_already_init) { // app shut down while loading
		svcReleaseMutex(small_resource_lock);
		return;
	}
	comment = new_comment; // do not apply to the cache because it's a mess to also save the folding status
	comment_view->replies.insert(comment_view->replies.end(), new_reply_views.begin(), new_reply_views.end());
	comment_view->replies_shown = comment_view->replies.size();
	comment_view->is_loading_replies = false;
	svcReleaseMutex(small_resource_lock);
	var_need_reflesh = true;
}
static void load_caption(void *arg_) {
	auto *arg = (std::pair<std::string *, std::string *> *) arg_;
	auto &base_lang_id = *arg->first;
	auto &translation_lang_id = *arg->second;
	
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto new_video_info = youtube_video_page_load_caption(cur_video_info, base_lang_id, translation_lang_id);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	
	std::vector<View *> caption_main_views;
	
	int top_button_height = DEFAULT_FONT_INTERVAL * 1.5;
	int top_button_width = 140;
	caption_main_views.push_back((new HorizontalListView(0, 0, top_button_height))
		->set_views({
			(new SelectorView(0, 0, 320 - top_button_width, top_button_height))
				->set_texts({
					(std::function<std::string ()>) [] () { return LOCALIZED(OFF); },
					(std::function<std::string ()>) [] () { return LOCALIZED(ON); }
				}, 1)
				->set_on_change([] (const SelectorView &view) { caption_overlay_view->set_is_visible(view.selected_button); }),
			(new TextView(0, 0, top_button_width, top_button_height))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SELECT_LANGUAGE); })
				->set_text_offset(0.0, -2.0)
				->set_x_alignment(TextView::XAlign::CENTER)
				->set_on_view_released([] (View &view) {
					captions_tab_view = caption_language_select_view;
				})
				->set_background_color(COLOR_GRAY(0x80))
		})
	);
	if (!new_video_info.caption_data[{base_lang_id, translation_lang_id}].size()) {
		caption_main_views.push_back((new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
			->set_text((std::function<std::string ()>) [] () { return LOCALIZED(NO_CAPTION); })
			->set_x_alignment(TextView::XAlign::CENTER)
		);
	}
	for (const auto &caption_piece : new_video_info.caption_data[{base_lang_id, translation_lang_id}]) {
		const auto &cur_content = caption_piece.content;
		
		if (!cur_content.size() || cur_content == "\n") continue;
		
		std::vector<std::string> cur_lines;
		auto itr = cur_content.begin();
		while (itr != cur_content.end()) {
			auto next_itr = std::find(itr, cur_content.end(), '\n');
			auto tmp = truncate_str(std::string(itr, next_itr), CAPTION_CONTENT_MAX_WIDTH, 20, 0.5, 0.5);
			cur_lines.insert(cur_lines.end(), tmp.begin(), tmp.end());
			
			if (next_itr != cur_content.end()) itr = std::next(next_itr);
			else break;
		}
		
		float start_time = caption_piece.start_time;
		float end_time = caption_piece.end_time;
		caption_main_views.push_back((new HorizontalListView(0, 0, DEFAULT_FONT_INTERVAL * cur_lines.size() + SMALL_MARGIN))
			->set_views({
				(new TextView(0, 0, CAPTION_TIMESTAMP_WIDTH, DEFAULT_FONT_INTERVAL))
					->set_text(Util_convert_seconds_to_time(start_time))
					->set_text_offset(0.0, -1.0)
					->set_get_text_color([] () { return COLOR_LINK; })
					->set_on_view_released([start_time] (View &view) {
						if (network_decoder.ready) send_seek_request_wo_lock(start_time);
					}),
				(new TextView(0, 0, CAPTION_CONTENT_MAX_WIDTH, DEFAULT_FONT_INTERVAL * cur_lines.size()))
					->set_text_lines(cur_lines)
					->set_text_offset(0.0, -1.0)
					->set_get_background_color([start_time, end_time] (const View &) {
						return vid_current_pos >= start_time && vid_current_pos < end_time ? LIGHT1_BACK_COLOR : DEFAULT_BACK_COLOR;
					})
			})
		);
	}
	caption_main_views.push_back(new EmptyView(0, 0, 320, SMALL_MARGIN));
	
	// caption overlay
	CaptionOverlayView *new_caption_overlay_view = (new CaptionOverlayView(0, 0, 400, 240))
		->set_caption_data(new_video_info.caption_data[{base_lang_id, translation_lang_id}]);
	
	
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (!vid_already_init) { // app shut down while loading
		svcReleaseMutex(small_resource_lock);
		return;
	}
	caption_main_view->recursive_delete_subviews();
	caption_main_view->reset();
	caption_main_view->set_views(caption_main_views);
	captions_tab_view = caption_main_view;
	
	delete caption_overlay_view;
	caption_overlay_view = new_caption_overlay_view;
	caption_overlay_view->set_is_visible(true);
	
	svcReleaseMutex(small_resource_lock);
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

// should be called while `small_resource_lock` is locked
static void send_change_video_request_wo_lock(std::string url, bool update_player, bool update_view, bool force_load) {
	remove_all_async_tasks_with_type(load_video_page);
	remove_all_async_tasks_with_type(load_more_suggestions);
	remove_all_async_tasks_with_type(load_more_comments);
	
	if (force_load) video_info_cache.erase(url);
	
	if (update_player) {
		if (cur_playing_url != url) {
			vid_play_request = false;
			cur_playing_url = url;
		}
	}
	if (update_view) {
		if (cur_displaying_url != url) {
			cur_displaying_url = url;
			if (selected_tab != TAB_PLAYLIST) selected_tab = TAB_GENERAL;
		}
	}
	if (update_player && update_view) queue_async_task(load_video_page, NULL);
	else if (update_view) queue_async_task(load_video_page, &cur_displaying_url);
	else if (update_player) queue_async_task(load_video_page, &cur_playing_url);
	var_need_reflesh = true;
}
static void send_change_video_request(std::string url, bool update_player, bool update_view, bool force_load) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	send_change_video_request_wo_lock(url, update_player, update_view, force_load);
	svcReleaseMutex(small_resource_lock);
}

static void send_seek_request_wo_lock(double pos) {
	vid_seek_pos = pos;
	vid_current_pos = pos;
	vid_seek_request = true;
	if (network_decoder.ready) // avoid locking while initing
		network_decoder.interrupt = true;
}
static void send_seek_request(double pos) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	send_seek_request_wo_lock(pos);
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
		float time_str0_w = Draw_get_width(time_str0, SMALL_FONT_SIZE);
		float time_str1_w = Draw_get_width(time_str1, SMALL_FONT_SIZE);
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
			var_need_reflesh = true;
		}
		if (bar_grabbed)
			last_grab_timestamp = network_decoder.get_timestamp_from_bar_pos(((key.touch_x != -1 ? key.touch_x : last_touch_x) - bar_x_l) / (bar_x_r - bar_x_l));
		if (bar_grabbed && key.touch_x == -1) {
			if (network_decoder.ready) send_seek_request(last_grab_timestamp);
			bar_grabbed = false;
			var_need_reflesh = true;
		}
		if (key.p_touch && key.touch_x >= 320 - MAXIMIZE_ICON_WIDTH && key.touch_y >= 240 - VIDEO_PLAYING_BAR_HEIGHT) {
			intent->next_scene = SceneType::VIDEO_PLAYER;
			intent->arg = cur_playing_url;
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
					if (eof_reached) send_seek_request_wo_lock(0);
					Util_speaker_resume(0);
				} else {
					vid_pausing = true;
					Util_speaker_pause(0);
				}
			} else if (cur_video_info.is_playable()) {
				network_waiting_status = NULL;
				send_change_video_request_wo_lock(cur_displaying_url, true, false, false);
			}
		}
		svcReleaseMutex(small_resource_lock);
		
		last_touch_x = key.touch_x;
		last_touch_y = key.touch_y;
	}
}
void video_update_playing_bar(Hid_info key, Intent *intent) { Bar::video_update_playing_bar(key, intent); }
void video_draw_playing_bar() { Bar::video_draw_playing_bar(); }


void video_set_linear_filter_enabled(bool enabled) {
	if (vid_already_init) {
		for(int i = 0; i < 2; i++)
			Draw_c2d_image_set_filter(&vid_image[i], enabled);
	}
}
void video_set_skip_drawing(bool skip) { video_skip_drawing = skip; }


static void decode_thread(void* arg) {
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
			eof_reached = false;
			vid_play_request = true;
			vid_total_time = 0;
			vid_total_frames = 0;
			vid_min_time = 99999999;
			vid_max_time = 0;
			vid_recent_total_time = 0;
			for(int i = 0; i < 90; i++)
				vid_recent_time[i] = 0;
			
			for(int i = 0; i < 2; i++)
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
			
			// video page parsing sometimes randomly fails, so try several times
			network_waiting_status = "Reading Stream";
			if (audio_only_mode) {
				result = network_decoder.init(playing_video_info.audio_stream_url, stream_downloader,
					playing_video_info.is_livestream ? playing_video_info.stream_fragment_len : -1, playing_video_info.needs_timestamp_adjusting(), var_is_new3ds);
			} else if (video_p_value == 360 && playing_video_info.duration_ms <= 60 * 60 * 1000 && playing_video_info.both_stream_url != "") {
				// itag 18 (both_stream) of a long video takes too much time and sometimes leads to a crash 
				result = network_decoder.init(playing_video_info.both_stream_url, stream_downloader,
					playing_video_info.is_livestream ? playing_video_info.stream_fragment_len : -1, playing_video_info.needs_timestamp_adjusting(), var_is_new3ds);
			} else if (playing_video_info.video_stream_urls[(int) video_p_value] != "" && playing_video_info.audio_stream_url != "") {
				result = network_decoder.init(playing_video_info.video_stream_urls[(int) video_p_value], playing_video_info.audio_stream_url, stream_downloader,
					playing_video_info.is_livestream ? playing_video_info.stream_fragment_len : -1, playing_video_info.needs_timestamp_adjusting(), var_is_new3ds && video_p_value == 360);
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
					send_change_video_request(cur_playing_url, true, false, true);
				} else {
					Util_err_set_error_message(result.string, result.error_description, DEF_SAPP0_DECODE_THREAD_STR, result.code);
					Util_err_set_error_show_flag(true);
					var_need_reflesh = true;
				}
				vid_play_request = false;
			}
			network_waiting_status = NULL;
			
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
					vid_frametime = 1000.0 / vid_framerate;

					if(vid_width % 16 != 0)
						vid_width += 16 - vid_width % 16;
					if(vid_height % 16 != 0)
						vid_height += 16 - vid_height % 16;
				}
			}
			
			if (seek_at_init_request >= 0) {
				vid_seek_request = true;
				vid_seek_pos = seek_at_init_request;
				seek_at_init_request = -1;
			}
			
			osTickCounterUpdate(&counter1);
			while (vid_play_request)
			{
				if (vid_seek_request && !vid_change_video_request) {
					network_waiting_status = "Seeking";
					Util_speaker_clear_buffer(0);
					svcWaitSynchronization(network_decoder_critical_lock, std::numeric_limits<s64>::max()); // the converter thread is now suspended
					vid_current_pos = vid_seek_pos;
					while (vid_seek_request && !vid_change_video_request && vid_play_request) {
						svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
						double seek_pos_bak = vid_seek_pos;
						vid_seek_request = false;
						network_decoder.interrupt = false;
						svcReleaseMutex(small_resource_lock);
						
						result = network_decoder.seek(seek_pos_bak * 1000 * 1000); // nano seconds
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
					if (eof_reached) vid_pausing = false;
					network_waiting_status = NULL;
					var_need_reflesh = true;
				}
				if (vid_change_video_request || !vid_play_request) break;
				vid_duration = network_decoder.get_duration();
				
				auto type = network_decoder.next_decode_type();
				
				if (type == NetworkMultipleDecoder::PacketType::EoF) {
					vid_pausing = true;
					if (!eof_reached) { // the first time it reaches EOF
						svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
						if ((var_autoplay_level == 2 && playing_video_info.has_next_video()) ||
							(var_autoplay_level == 1 && playing_video_info.has_next_video_in_playlist()))
								send_change_video_request_wo_lock(playing_video_info.get_next_video().url, true, false, false);
						svcReleaseMutex(small_resource_lock);
					}
					eof_reached = true;
					usleep(10000);
					continue;
				} else eof_reached = false;
				
				if (type == NetworkMultipleDecoder::PacketType::AUDIO) {
					osTickCounterUpdate(&counter0);
					result = network_decoder.decode_audio(&audio_size, &audio, &pos);
					osTickCounterUpdate(&counter0);
					vid_audio_time = osTickCounterRead(&counter0);
					
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
				} else if (type == NetworkMultipleDecoder::PacketType::VIDEO) {
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
						if (result.code != 0 && result.code != DEF_ERR_NEED_MORE_INPUT)
							Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_video_decoder_decode()..." + result.string + result.error_description, result.code);
					}
				} else if (type == NetworkMultipleDecoder::PacketType::INTERRUPTED) continue;
				else Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "unknown type of packet");
			}
			
			network_waiting_status = NULL;
			
			while (Util_speaker_is_playing(0) && vid_play_request) usleep(10000);
			Util_speaker_exit(0);
			
			if(!vid_change_video_request)
				vid_play_request = false;
			
			// make sure the convert thread stops before closing network_decoder
			svcWaitSynchronization(network_decoder_critical_lock, std::numeric_limits<s64>::max()); // the converter thread is now suspended
			network_decoder.deinit();
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

static void convert_thread(void* arg) {
	Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Thread started.");
	u8* yuv_video = NULL;
	u8* video = NULL;
	TickCounter counter0, counter1;
	Result_with_string result;

	osTickCounterStart(&counter0);
	
	y2rInit();

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
				} while (vid_play_request && !vid_seek_request && !vid_change_video_request && !audio_only_mode);
				
				if (audio_only_mode) {
					while (audio_only_mode && vid_play_request && !vid_seek_request && !vid_change_video_request) {
						double tmp = Util_speaker_get_current_timestamp(0, vid_sample_rate);
						if (tmp != -1) vid_current_pos = tmp;
						usleep(50000);
					}
					break;
				}
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
					result = Util_converter_y2r_yuv420p_to_bgr565(yuv_video, &video, vid_width, vid_height, false);
					video_need_free = true;
				}
				osTickCounterUpdate(&counter0);
				vid_convert_time = osTickCounterRead(&counter0);
				
				double cur_convert_time = 0;
				
				if(result.code == 0)
				{
					vid_tex_width[vid_mvd_image_num] = vid_width_org;
					vid_tex_height[vid_mvd_image_num] = vid_height_org;
					
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
					vid_current_pos = pts;
					
					osTickCounterUpdate(&counter0);
					osTickCounterUpdate(&counter1);
					
					if (!video_skip_drawing) {
						result = Draw_set_texture_data(&vid_image[vid_mvd_image_num], video, vid_width, vid_height_org, 1024, 1024, GPU_RGB565);
						if(result.code != 0)
							Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
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
	
	y2rExit();
	
	Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Thread exit.");
	threadExit(0);
}

void video_set_show_debug_info(bool show) {
	if (vid_already_init) {
		svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
		if (show) {
			if (playback_tab_view->views.back() != debug_info_view) playback_tab_view->views.push_back(debug_info_view);
		} else {
			if (playback_tab_view->views.back() == debug_info_view) playback_tab_view->views.pop_back();
		}
		svcReleaseMutex(small_resource_lock);
	}
}


#define DURATION_FONT_SIZE 0.4
static void draw_video_content(Hid_info key) {
	svcWaitSynchronization(small_resource_lock, std::numeric_limits<s64>::max());
	if (selected_tab == TAB_PLAYLIST) playlist_view->draw();
	else if (selected_tab == TAB_PLAYBACK) playback_tab_view->draw();
	else if (is_loading) Draw_x_centered(LOCALIZED(LOADING), 0, 320, 0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
	else {
		if (selected_tab == TAB_GENERAL) main_tab_view->draw();
		else if (selected_tab == TAB_SUGGESTIONS) suggestion_view->draw();
		else if (selected_tab == TAB_COMMENTS) comment_all_view->draw();
		else if (selected_tab == TAB_CAPTIONS) captions_tab_view->draw();
	}
	
	svcReleaseMutex(small_resource_lock);
}

Intent VideoPlayer_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	
	int image_num = !vid_mvd_image_num;

	thumbnail_set_active_scene(SceneType::VIDEO_PLAYER);
	
	//fit to screen size
	vid_zoom = std::min(401.0 / vid_width_org, (var_full_screen_mode ? 241.0 : 226.0) / vid_height_org);
	vid_zoom = std::min(10.0, std::max(0.05, vid_zoom));
	vid_x = (400 - (vid_width_org * vid_zoom)) / 2;
	vid_y = ((var_full_screen_mode ? 240 : 225) - (vid_height_org * vid_zoom)) / 2;
	if (!var_full_screen_mode) vid_y += 15;
	
	bool video_playing_bar_show = video_is_playing();
	
	if(var_need_reflesh || !var_eco_mode)
	{
		bool video_playing = vid_play_request && network_decoder.ready && !audio_only_mode;
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, video_playing ? DEF_DRAW_BLACK : DEFAULT_BACK_COLOR);

		if (video_playing) {
			//video
			Draw_texture(vid_image[image_num].c2d, vid_x, vid_y, vid_tex_width[image_num] * vid_zoom, vid_tex_height[image_num] * vid_zoom);
		} else Draw_texture(vid_banner[var_night_mode], 0, 15, 400, 225);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		if (!var_full_screen_mode || !network_decoder.ready) Draw_top_ui();
		if (var_debug_mode) Draw_debug_info();
		caption_overlay_view->cur_timestamp = vid_current_pos;
		caption_overlay_view->draw();

		Draw_screen_ready(1, DEFAULT_BACK_COLOR);

		draw_video_content(key);
		
		// tab selector
		Draw_texture(var_square_image[0], LIGHT1_BACK_COLOR, 0, CONTENT_Y_HIGH, 320, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], LIGHT2_BACK_COLOR, selected_tab * 320 / TAB_NUM, CONTENT_Y_HIGH, 320 / TAB_NUM + 1, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], LIGHT3_BACK_COLOR, selected_tab * 320 / TAB_NUM, CONTENT_Y_HIGH, 320 / TAB_NUM + 1, TAB_SELECTOR_SELECTED_LINE_HEIGHT);
		for (int i = 0; i < TAB_NUM; i++) {
			float font_size = 0.4;
			float x_l = i * 320 / TAB_NUM;
			float x_r = (i + 1) * 320 / TAB_NUM;
			std::string tab_string;
			if (i == TAB_GENERAL) tab_string = LOCALIZED(GENERAL);
			else if (i == TAB_SUGGESTIONS) tab_string = LOCALIZED(SUGGESTIONS);
			else if (i == TAB_COMMENTS) tab_string = LOCALIZED(COMMENTS);
			else if (i == TAB_CAPTIONS) tab_string = LOCALIZED(CAPTIONS);
			else if (i == TAB_PLAYBACK) tab_string = LOCALIZED(PLAYBACK);
			else if (i == TAB_PLAYLIST) tab_string = LOCALIZED(PLAYLIST);
			font_size *= std::min(1.0, (x_r - x_l) * 0.9 / Draw_get_width(tab_string, font_size));
			float y = CONTENT_Y_HIGH + (TAB_SELECTOR_HEIGHT - Draw_get_height(tab_string, font_size)) / 2 - 3;
			if (i == selected_tab) y += 1;
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
		// YES, this section needs refactoring, seriously
		if (cur_video_info.comments.size()) { // comments
			std::vector<std::pair<float, PostView *> > comments_list; // list of comment views whose author's thumbnails should be loaded
			{
				constexpr int LOW = -1000;
				constexpr int HIGH = 1240;
				float cur_y = -comment_all_view->get_offset();
				for (size_t i = 0; i < comments_main_view->views.size(); i++) {
					float cur_height = comments_main_view->views[i]->get_height();
					if (cur_y < HIGH && cur_y + cur_height >= LOW) {
						auto parent_comment_view = dynamic_cast<PostView *>(comments_main_view->views[i]);
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
			
			std::set<PostView *> newly_loading_views, cancelling_views;
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
				i->author_icon_handle = thumbnail_request(i->author_icon_url, SceneType::VIDEO_PLAYER, 0, ThumbnailType::ICON);
				comment_thumbnail_loaded_list.insert(i);
			}
			
			std::vector<std::pair<int, int> > priority_list;
			auto priority = [&] (float i) { return 500 + (i < 0 ? i : 240 - i) / 100; };
			for (auto i : comments_list) priority_list.push_back({i.second->author_icon_handle, priority(i.first)});
			thumbnail_set_priorities(priority_list);
		}
		if (cur_video_info.playlist.videos.size()) { // playlist
			int item_num = cur_video_info.playlist.videos.size();
			int displayed_l = std::min(item_num, std::max(0, playlist_list_view->get_offset() / SUGGESTIONS_VERTICAL_INTERVAL));
			int displayed_r = std::min(item_num, std::max(0, (playlist_list_view->get_offset() + (CONTENT_Y_HIGH - PLAYLIST_TOP_HEIGHT) - 1) / SUGGESTIONS_VERTICAL_INTERVAL + 1));
			int request_target_l = std::max(0, displayed_l - (MAX_THUMBNAIL_LOAD_REQUEST - (displayed_r - displayed_l)) / 2);
			int request_target_r = std::min(item_num, request_target_l + MAX_THUMBNAIL_LOAD_REQUEST);
			// transition from [thumbnail_request_l, thumbnail_request_r) to [request_target_l, request_target_r)
			std::set<int> new_indexes, cancelling_indexes;
			for (int i = playlist_thumbnail_request_l; i < playlist_thumbnail_request_r; i++) cancelling_indexes.insert(i);
			for (int i = request_target_l; i < request_target_r; i++) new_indexes.insert(i);
			for (int i = playlist_thumbnail_request_l; i < playlist_thumbnail_request_r; i++) new_indexes.erase(i);
			for (int i = request_target_l; i < request_target_r; i++) cancelling_indexes.erase(i);
			
			for (auto i : cancelling_indexes) {
				SuccinctVideoView *cur_view = dynamic_cast<SuccinctVideoView *>(playlist_list_view->views[i]);
				thumbnail_cancel_request(cur_view->thumbnail_handle);
				cur_view->thumbnail_handle = -1;
			}
			for (auto i : new_indexes) {
				SuccinctVideoView *cur_view = dynamic_cast<SuccinctVideoView *>(playlist_list_view->views[i]);
				cur_view->thumbnail_handle = thumbnail_request(cur_view->thumbnail_url, SceneType::VIDEO_PLAYER, 0, ThumbnailType::VIDEO_THUMBNAIL);
			}
			
			playlist_thumbnail_request_l = request_target_l;
			playlist_thumbnail_request_r = request_target_r;
			
			std::vector<std::pair<int, int> > priority_list(request_target_r - request_target_l);
			auto dist = [&] (int i) { return i < displayed_l ? displayed_l - i : i - displayed_r + 1; };
			for (int i = request_target_l; i < request_target_r; i++) priority_list[i - request_target_l] = {
				dynamic_cast<SuccinctVideoView *>(playlist_list_view->views[i])->thumbnail_handle, 500 - dist(i)};
			thumbnail_set_priorities(priority_list);
		}
		
		update_overlay_menu(&key, &intent, SceneType::VIDEO_PLAYER);
		if (selected_tab == TAB_GENERAL) main_tab_view->update(key);
		else if (selected_tab == TAB_SUGGESTIONS) suggestion_view->update(key);
		else if (selected_tab == TAB_COMMENTS) comment_all_view->update(key);
		else if (selected_tab == TAB_CAPTIONS) captions_tab_view->update(key);
		else if (selected_tab == TAB_PLAYBACK) playback_tab_view->update(key);
		else if (selected_tab == TAB_PLAYLIST) playlist_view->update(key);
		
		if (channel_url_pressed != "") {
			intent.next_scene = SceneType::CHANNEL;
			intent.arg = channel_url_pressed;
			channel_url_pressed = "";
		}
		if (suggestion_clicked_url != "") {
			intent.next_scene = SceneType::VIDEO_PLAYER;
			intent.arg = suggestion_clicked_url;
			suggestion_clicked_url = "";
		}
		
		// tab selector
		{
			auto released_point = tab_selector_scroller.update(key, TAB_SELECTOR_HEIGHT);
			if (released_point.first != -1) {
				int next_tab = released_point.first * TAB_NUM / 320;
				selected_tab = next_tab;
				var_need_reflesh = true;
			}
		}
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		if (key.p_a) {
			if(vid_play_request) {
				if (vid_pausing) {
					if (!vid_pausing_seek) Util_speaker_resume(0);
					if (eof_reached) send_seek_request_wo_lock(0);
					vid_pausing = false;
				} else {
					if (!vid_pausing_seek) Util_speaker_pause(0);
					vid_pausing = true;
				}
			}

			var_need_reflesh = true;
		} else if ((key.h_x && key.p_b) || (key.h_b && key.p_x)) {
			vid_play_request = false;
			var_need_reflesh = true;
		} else if (key.p_b) {
			intent.next_scene = SceneType::BACK;
		} else if (key.p_d_right || key.p_d_left) {
			if (network_decoder.ready) {
				double pos = vid_current_pos;
				pos += key.p_d_right ? 10 : -10;
				pos = std::max<double>(0, pos);
				pos = std::min<double>(vid_duration, pos);
				send_seek_request_wo_lock(pos);
			}
		} else if (key.h_touch || key.p_touch) var_need_reflesh = true;
		
		svcReleaseMutex(small_resource_lock);
		/* ****************************** LOCK END ******************************  */
		
		static int consecutive_scroll = 0;
		if (key.h_c_up || key.h_c_down) {
			if (key.h_c_up) consecutive_scroll = std::max(0, consecutive_scroll) + 1;
			else consecutive_scroll = std::min(0, consecutive_scroll) - 1;
			
			float scroll_amount = DPAD_SCROLL_SPEED0;
			if (std::abs(consecutive_scroll) > DPAD_SCROLL_SPEED1_THRESHOLD) scroll_amount = DPAD_SCROLL_SPEED1;
			if (key.h_c_up) scroll_amount *= -1;
			
			if (selected_tab == TAB_GENERAL) main_tab_view->scroll(scroll_amount);
			else if (selected_tab == TAB_SUGGESTIONS) suggestion_view->scroll(scroll_amount);
			else if (selected_tab == TAB_COMMENTS) comment_all_view->scroll(scroll_amount);
			else if (selected_tab == TAB_CAPTIONS) {
				if (captions_tab_view == caption_main_view) caption_main_view->scroll(scroll_amount);
			} else if (selected_tab == TAB_PLAYBACK) playback_tab_view->scroll(scroll_amount);
			else if (selected_tab == TAB_PLAYLIST) playlist_list_view->scroll(scroll_amount);
			var_need_reflesh = true;
		} else consecutive_scroll = 0;
		
		if ((key.h_x && key.p_y) || (key.h_y && key.p_x)) var_debug_mode = !var_debug_mode;
		if ((key.h_x && key.p_a) || (key.h_x && key.p_a)) var_show_fps = !var_show_fps;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
