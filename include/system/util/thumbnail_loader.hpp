#pragma once
#include "types.hpp"


void request_thumbnail(std::string url);

void cancel_request_thumbnail(std::string url);

bool thumbnail_available(std::string url);

bool draw_thumbnail(std::string url, int x_offset, int y_offset, int x_len, int y_len);



void thumbnail_downloader_thread_func(void *arg);
void thumbnail_downloader_thread_exit_request(void);

