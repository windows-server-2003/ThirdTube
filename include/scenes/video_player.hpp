#pragma once

bool VideoPlayer_query_init_flag(void);

bool VideoPlayer_query_running_flag(void);

void VideoPlayer_resume(std::string arg);

void VideoPlayer_suspend(void);

void VideoPlayer_init(void);

void VideoPlayer_exit(void);

Intent VideoPlayer_draw(void);
