#pragma once
#include "types.hpp"
#include "scene_switcher.hpp"

enum class ThumbnailType {
	DEFAULT,
	VIDEO_THUMBNAIL, // default.jpg is offered in 4:3 aspect ratio, so trim to 16:9
	VIDEO_BANNER, // if width is 1060, it's manually trimmed into 1024
	ICON, // rounded
};

// returns the 'handle' of the thumbnail
int thumbnail_request(const std::string &url, SceneType scene_id, int priority, ThumbnailType type = ThumbnailType::DEFAULT);
void thumbnail_cancel_request(int handle);
void thumbnail_set_priority(int handle, int priority);
void thumbnail_set_priorities(const std::vector<std::pair<int, int> > &priority_list);

void thumbnail_set_active_scene(SceneType type);

bool thumbnail_is_available(const std::string &url);
bool thumbnail_is_available(int handle);

bool thumbnail_draw(int handle, int x_offset, int y_offset, int x_len, int y_len);



void thumbnail_downloader_thread_func(void *arg);
void thumbnail_downloader_thread_exit_request(void);

