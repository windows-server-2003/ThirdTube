#include "headers.hpp"
#include <map>
#include <queue>

struct LoadedThumbnail {
	int image_width;
	int image_height;
	int texture_width;
	int texture_height;
	Image_data data;
};

struct Request {
	std::string url;
	SceneType scene;
	int priority;
};

static volatile SceneType active_scene = SceneType::SEARCH;
static Handle resource_lock;
static bool lock_initialized = false;
static std::vector<Request> requests;
static std::queue<int> free_list;

struct URLStatus {
	std::set<int> handles;
	bool is_loaded = false;
	LoadedThumbnail data;
	ThumbnailType type;
};
static std::map<std::string, URLStatus> requested_urls;

static void lock() {
	if (!lock_initialized) {
		svcCreateMutex(&resource_lock, false);
		lock_initialized = true;
	}
	svcWaitSynchronization(lock_initialized, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(lock_initialized);
}

int thumbnail_request(const std::string &url, SceneType scene_id, int priority, ThumbnailType type) {
	if (url == "") return -1;
	int handle;
	lock();
	if (free_list.size()) {
		handle = free_list.front();
		free_list.pop();
		requests[handle] = {url, scene_id, priority};
	} else {
		handle = requests.size();
		requests.push_back({url, scene_id, priority});
	}
	requested_urls[url].handles.insert(handle);
	requested_urls[url].type = type;
	release();
	return handle;
}
void thumbnail_cancel_request(int handle) {
	if (handle == -1) return;
	lock();
	std::string url = requests[handle].url;
	auto &url_status = requested_urls[url];
	url_status.handles.erase(handle);
	if (!url_status.handles.size()) {
		if (url_status.is_loaded) Draw_c2d_image_free(url_status.data.data);
		requested_urls.erase(url);
	}
	free_list.push(handle);
	release();
}
void thumbnail_set_priority(int handle, int value) {
	if (handle == -1) return;
	lock();
	requests[handle].priority = value;
	release();
}
void thumbnail_set_priorities(const std::vector<std::pair<int, int> > &priority_list) {
	lock();
	for (auto i : priority_list) {
		if (i.first == -1) continue;
		requests[i.first].priority = i.second;
	}
	release();
}
void thumbnail_set_active_scene(SceneType type) {
	active_scene = type;
}
bool thumbnail_is_available(const std::string &url) {
	if (url == "") return false;
	lock();
	bool res = requested_urls.count(url) && requested_urls[url].is_loaded;
	release();
	return res;
}
bool thumbnail_is_available(int handle) {
	if (handle == -1) return false;
	return thumbnail_is_available(requests[handle].url); // TODO : lock
}
bool thumbnail_draw(int handle, int x_offset, int y_offset, int x_len, int y_len) {
	if (handle == -1) return false;
	bool res;
	lock();
	std::string url = requests[handle].url;
	if (requested_urls.count(url) && requested_urls[url].is_loaded) {
		LoadedThumbnail thumbnail = requested_urls[url].data;
		Draw_texture(thumbnail.data.c2d, x_offset, y_offset, x_len, y_len);
		res = true;
	} else res = false;
	release();
	return res;
}


static std::vector<u8> http_get(const std::string &url) {
	constexpr int BLOCK = 0x40000; // 256 KB
	add_cpu_limit(30);
	// use mobile version of User-Agent for smaller webpage (and the whole parser is designed to parse the mobile version)
	auto network_res = access_http_get(url, {{"User-Agent", "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36"}});
	std::vector<u8> res;
	if (network_res.first == "") {
		std::vector<u8> buffer(BLOCK);
		while (1) {
			u32 len_read;
			Result ret = httpcDownloadData(&network_res.second, &buffer[0], BLOCK, &len_read);
			res.insert(res.end(), buffer.begin(), buffer.begin() + len_read);
			if (ret != (s32) HTTPC_RESULTCODE_DOWNLOADPENDING) break;
		}
		
		httpcCloseContext(&network_res.second);
	} else Util_log_save("thumb-dl", "failed accessing : " + network_res.first);
	
	remove_cpu_limit(30);
	return res;
}


static bool should_be_running = true;
void thumbnail_downloader_thread_func(void *arg) {
	while (should_be_running) {
		const std::string *next_url_ = NULL;
		ThumbnailType next_type = ThumbnailType::DEFAULT;
		lock();
		{
			int max_priority = -1;
			for (auto &i : requested_urls) {
				if (i.second.is_loaded) continue;
				int cur_url_priority = 0;
				for (auto handle : i.second.handles) {
					int cur_priority = requests[handle].priority;
					if (requests[handle].scene == active_scene) cur_priority += 1000000;
					cur_url_priority = std::max(cur_url_priority, cur_priority);
				}
				if (max_priority < cur_url_priority) {
					max_priority = cur_url_priority;
					next_url_ = &i.first;
					next_type = i.second.type;
				}
			}
		}
		std::string next_url;
		if (next_url_) next_url = *next_url_;
		release();
		
		if (!next_url_) {
			usleep(50000);
			continue;
		}
		// Util_log_save("thumb-dl", "size:" + std::to_string(requests.size()));
		auto encoded_data = http_get(next_url);
		
		int w, h;
		u8 *decoded_data = Image_decode(&encoded_data[0], encoded_data.size(), &w, &h);
		if (decoded_data) {
			// some special operations on the picture here (it shouldn't be here but...)
			// for video thumbnail, crop to 16:9
			if (next_type == ThumbnailType::VIDEO_THUMBNAIL && h > w * 9 / 16 + 1) {
				int new_h = w * 9 / 16;
				int vertical_offset = (h - new_h) / 2;
				memmove(decoded_data, decoded_data + vertical_offset * w * 2, new_h * w * 2);
				h = new_h;
			}
			// channel banner : crop to 1024 to fit in the maximum texture size
			if (next_type == ThumbnailType::VIDEO_BANNER && w == 1060) {
				for (int i = 0; i < h; i++) memmove(decoded_data + i * 1024 * 2, decoded_data + (i * 1060 + 18) * 2, 1024 * 2);
				w = 1024;
			}
			// channel icon : round (definitely not the recommended way but we will fill the area outside the circle with white)
			if (next_type == ThumbnailType::ICON) {
				float radius = (float) h / 2;
				for (int i = 0; i < h; i++) for (int j = 0; j < w; j++) {
					float distance = std::hypot(radius - (i + 0.5), radius - (j + 0.5));
					u8 b = decoded_data[(i * w + j) * 2 + 0] & ((1 << 5) - 1);
					u8 g = (decoded_data[(i * w + j) * 2 + 0] >> 5) | ((decoded_data[(i * w + j) * 2 + 1] & ((1 << 3) - 1)) << 3);
					u8 r = decoded_data[(i * w + j) * 2 + 1] >> 3;
					float proportion = std::max(0.0f, std::min(1.0f, radius + 0.5f - distance));
					b = b * proportion + ((1 << 5) - 1) * (1 - proportion);
					g = g * proportion + ((1 << 6) - 1) * (1 - proportion);
					r = r * proportion + ((1 << 5) - 1) * (1 - proportion);
					decoded_data[(i * w + j) * 2 + 0] = b | g << 5;
					decoded_data[(i * w + j) * 2 + 1] = g >> 3 | r << 3;
				}
			}
			
			
			Image_data result_image;
			int texture_w = 1;
			while (texture_w < w) texture_w <<= 1;
			int texture_h = 1;
			while (texture_h < h) texture_h <<= 1;
			
			Result_with_string result;
			result = Draw_c2d_image_init(&result_image, texture_w, texture_h, GPU_RGB565);
			if (result.code != 0) {
				Util_log_save("thumb-dl", "out of linearmem");
			} else {
				result = Draw_set_texture_data(&result_image, decoded_data, w, h, texture_w, texture_h, GPU_RGB565);
				if (result.code != 0) {
					Util_log_save("thumb-dl", "Draw_set_texture_data() failed");
				} else {
					lock();
					if (requested_urls.count(next_url)) { // in case the request is cancelled while downloading
						requested_urls[next_url].is_loaded = true;
						requested_urls[next_url].data = {w, h, texture_w, texture_h, result_image};
					}
					release();
				}
			}
			free(decoded_data);
			decoded_data = NULL;
		} else Util_log_save("thumb-dl", "Image_decode() failed");
	}
	
	lock();
	for (auto i : requested_urls) if (i.second.is_loaded) Draw_c2d_image_free(i.second.data.data);
	requested_urls.clear();
	release();
	
	Util_log_save("thumb-dl", "Thread exit.");
	threadExit(0);
}
void thumbnail_downloader_thread_exit_request() {
	should_be_running = false;
}

