#pragma once
#include "types.hpp"

#define VIDEO_PLAYING_BAR_HEIGHT 20
bool video_is_playing(void);
void video_draw_playing_bar();
void video_update_playing_bar(Hid_info key, Intent *intent);

void video_set_linear_filter_enabled(bool enabled);
void video_set_show_debug_info(bool show);
void video_set_skip_drawing(bool skip);

bool VideoPlayer_query_init_flag(void);

bool VideoPlayer_query_running_flag(void);

void VideoPlayer_resume(std::string arg);

void VideoPlayer_suspend(void);

void VideoPlayer_init(void);

void VideoPlayer_exit(void);

Intent VideoPlayer_draw(void);
