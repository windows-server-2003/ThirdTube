#include "headers.hpp"
#include "network/network_io.hpp"
#include "network/thumbnail_loader.hpp"
#include <set>
#include <map>
#include <queue>

static bool thread_network_session_list_inited = false;
static NetworkSessionList thread_network_session_list;
static void confirm_thread_network_session_list_inited() {
	if (!thread_network_session_list_inited) {
		thread_network_session_list_inited = true;
		thread_network_session_list.init();
	}
}

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

static std::map<std::string, std::vector<u8> > thumbnail_cache;
static int thumbnail_free_time_cnter = 0; // incremented each time when a thumbnail is freed
static std::map<std::string, int> thumbnail_free_time;

#define THUMBNAIL_CACHE_MAX 300 // 4 KB * 300 = 1.2 MB


struct URLStatus {
	std::set<int> handles;
	bool is_loaded = false;
	bool error = false;
	bool waiting_retry = false;
	time_t next_retry;
	int last_status_code = -2;
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
	thumbnail_free_time.erase(url);
	release();
	if (requests.size() > 180) Util_log_save("tloader", "WARNING : request size too large, possible resource leak : " + std::to_string(requests.size()));
	return handle;
}
inline static void thumbnail_cancel_request_wo_lock(int handle) {
	if (handle == -1) return;
	std::string url = requests[handle].url;
	auto &url_status = requested_urls[url];
	url_status.handles.erase(handle);
	if (!url_status.handles.size()) {
		if (url_status.is_loaded) Draw_c2d_image_free(url_status.data.data);
		requested_urls.erase(url);
		thumbnail_free_time[url] = ++thumbnail_free_time_cnter;
	}
	free_list.push(handle);
}
void thumbnail_cancel_request(int handle) {
	if (handle == -1) return;
	lock();
	thumbnail_cancel_request_wo_lock(handle);
	release();
}
void thumbnail_cancel_requests(const std::vector<int> &handles) {
	lock();
	for (auto handle : handles) if (handle != -1) thumbnail_cancel_request_wo_lock(handle);
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

int thumbnail_get_status_code(const std::string &url) {
	if (url == "") return false;
	lock();
	int res = requested_urls.count(url) ? requested_urls[url].last_status_code : -2;
	release();
	return res;
}
int thumbnail_get_status_code(int handle) {
	if (handle == -1) return false;
	return thumbnail_get_status_code(requests[handle].url);
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

static std::vector<u8> http_get(const std::string &url, int &status_code) {
	std::vector<u8> res;
	lock();
	if (thumbnail_cache.count(url)) res = thumbnail_cache[url];
	release();
	
	if (res.size()) {
		status_code = 0;
		return res;
	}
	
	confirm_thread_network_session_list_inited();
	auto result = thread_network_session_list.perform(HttpRequest::GET(url, {}));
	if (result.fail) {
		status_code = -1;
		Util_log_save("thumb-dl", "access fail : " + result.error);
	} else {
		if (result.data.size() && result.status_code / 100 == 2) {
			lock();
			if (thumbnail_cache.size() >= THUMBNAIL_CACHE_MAX) {
				std::string erase_url;
				int min_time = 1000000000;
				for (auto &item : thumbnail_cache) {
					if (!thumbnail_free_time.count(item.first)) continue;
					int cur_time = thumbnail_free_time[item.first];
					if (min_time > cur_time) {
						min_time = cur_time;
						erase_url = item.first;
					}
				}
				if (erase_url != "") thumbnail_cache.erase(erase_url);
			}
			thumbnail_cache[url] = result.data;
			
			if (thumbnail_cache.size() >= THUMBNAIL_CACHE_MAX + 10) Util_log_save("tloader", "over caching : " + std::to_string(thumbnail_cache.size()));
			
			release();
		}
		status_code = result.status_code;
	}
	return result.data;
}

static bool should_be_running = true;
void thumbnail_downloader_thread_func(void *arg) {
	while (should_be_running) {
		lock();
		struct Item {
			int priority;
			std::string url;
			ThumbnailType type;
		};
		std::vector<Item> download_list;
		{
			for (auto &i : requested_urls) {
				if (i.second.is_loaded) continue;
				if (i.second.error && (!i.second.waiting_retry || time(NULL) < i.second.next_retry)) continue;
				int priority = 0;
				for (auto handle : i.second.handles) priority = std::max(priority,
					 requests[handle].priority + (requests[handle].scene == active_scene ? PRIORITY_ACTIVE_SCENE : 0));
				download_list.push_back({priority, i.first, i.second.type});
			}
		}
		release();
		
		if (!download_list.size()) {
			usleep(50000);
			continue;
		}
		
		// sort in the decreasing order of the priority
		std::sort(download_list.begin(), download_list.end(), [] (const auto &i, const auto &j) { return i.priority > j.priority; });
		
		std::string priority_str;
		for (auto i : download_list) priority_str += std::to_string(i.priority) + " ";
		if (download_list[0].priority >= PRIORITY_ACTIVE_SCENE + PRIORITY_FOREGROUND) // load thumbnails in the foreground first
			while (download_list.back().priority < PRIORITY_ACTIVE_SCENE + PRIORITY_FOREGROUND) download_list.pop_back();
		
		std::vector<NetworkResult> results(download_list.size());
		std::vector<int> uncached_index_list;
		std::vector<int> cached_index_list;
		for (size_t i = 0; i < download_list.size(); i++) {
			if (thumbnail_cache.count(download_list[i].url)) {
				results[i].status_code = 0; // cached
				results[i].data = thumbnail_cache[download_list[i].url];
				cached_index_list.push_back(i);
			} else uncached_index_list.push_back(i);
		}
		
		auto load_downloaded_thumbnail = [&] (int i) {
			auto &res = results[i];
			auto &info = download_list[i];
			
			int w, h;
			u8 *decoded_data = NULL;
			if (res.data.size()) decoded_data = Image_decode(&res.data[0], res.data.size(), &w, &h);
			if (decoded_data) {
				// update cache
				lock();
				if (thumbnail_cache.size() >= THUMBNAIL_CACHE_MAX) {
					std::string erase_url;
					int min_time = 1000000000;
					for (auto &item : thumbnail_cache) {
						if (!thumbnail_free_time.count(item.first)) continue;
						int cur_time = thumbnail_free_time[item.first];
						if (min_time > cur_time) {
							min_time = cur_time;
							erase_url = item.first;
						}
					}
					if (erase_url != "") thumbnail_cache.erase(erase_url);
				}
				thumbnail_cache[info.url] = res.data;
				if (thumbnail_cache.size() >= THUMBNAIL_CACHE_MAX + 10) Util_log_save("tloader", "over caching : " + std::to_string(thumbnail_cache.size()));
				release();
				
				// some special operations on the picture here (it shouldn't be here but...)
				// for video thumbnail, crop to 16:9
				if (info.type == ThumbnailType::VIDEO_THUMBNAIL && h > w * 9 / 16 + 1) {
					int new_h = w * 9 / 16;
					int vertical_offset = (h - new_h) / 2;
					memmove(decoded_data, decoded_data + vertical_offset * w * 2, new_h * w * 2);
					h = new_h;
				}
				// channel banner : crop to 1024 to fit in the maximum texture size
				if (info.type == ThumbnailType::VIDEO_BANNER && w == 1060) {
					for (int i = 0; i < h; i++) memmove(decoded_data + i * 1024 * 2, decoded_data + (i * 1060 + 18) * 2, 1024 * 2);
					w = 1024;
				}
				// channel icon : round (definitely not the recommended way but we will fill the area outside the circle with white)
				if (info.type == ThumbnailType::ICON) {
					float radius = (float) h / 2;
					for (int i = 0; i < h; i++) for (int j = 0; j < w; j++) {
						float distance = std::hypot(radius - (i + 0.5), radius - (j + 0.5));
						u8 b = decoded_data[(i * w + j) * 2 + 0] & ((1 << 5) - 1);
						u8 g = (decoded_data[(i * w + j) * 2 + 0] >> 5) | ((decoded_data[(i * w + j) * 2 + 1] & ((1 << 3) - 1)) << 3);
						u8 r = decoded_data[(i * w + j) * 2 + 1] >> 3;
						float proportion = std::max(0.0f, std::min(1.0f, radius + 0.5f - distance));
						b = b * proportion + (var_night_mode ? 0 : ((1 << 5) - 1)) * (1 - proportion);
						g = g * proportion + (var_night_mode ? 0 : ((1 << 6) - 1)) * (1 - proportion);
						r = r * proportion + (var_night_mode ? 0 : ((1 << 5) - 1)) * (1 - proportion);
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
				if (result.code != 0) Util_log_save("thumb-dl", "out of linearmem");
				else {
					result = Draw_set_texture_data(&result_image, decoded_data, w, h, texture_w, texture_h, GPU_RGB565);
					if (result.code != 0) Util_log_save("thumb-dl", "Draw_set_texture_data() failed");
					else {
						lock();
						if (requested_urls.count(info.url)) { // in case the request is cancelled while downloading
							requested_urls[info.url].is_loaded = true;
							requested_urls[info.url].data = {w, h, texture_w, texture_h, result_image};
						}
						release();
					}
				}
				free(decoded_data);
				decoded_data = NULL;
			} else {
				lock();
				if (requested_urls.count(info.url)) {
					requested_urls[info.url].is_loaded = false;
					requested_urls[info.url].error = true;
					if (res.status_code / 100 != 4 && res.status_code / 100 != 2) {
						requested_urls[info.url].waiting_retry = true;
						requested_urls[info.url].next_retry = time(NULL) + 3;
					} else requested_urls[info.url].waiting_retry = false;
					requested_urls[info.url].last_status_code = res.status_code;
				}
				release();
				std::string err_msg = "load failed (http code : " + std::to_string(res.status_code) + ") size:" + std::to_string(res.data.size()) +
					" err:" + res.error;
				
				Util_log_save("thumb-dl", err_msg);
			}
		};
		TickCounter counter;
		osTickCounterStart(&counter);
		// Util_log_save("thumb-dl", "load cached : " + std::to_string(cached_index_list.size()));
		osTickCounterUpdate(&counter);
		for (auto i : cached_index_list) load_downloaded_thumbnail(i);
		osTickCounterUpdate(&counter);
		// Util_log_save("thumb-dl", std::to_string((int) osTickCounterRead(&counter)) + " ms");
		
		// Util_log_save("thumb-dl", "start dl : " + std::to_string(uncached_index_list.size()));
		// load uncached thumbnails
		{
			confirm_thread_network_session_list_inited();
			std::vector<HttpRequest> requests;
			for (auto i : uncached_index_list) requests.push_back(HttpRequest::GET(download_list[i].url, {}).with_on_finish_callback([&](NetworkResult &res, int index) {
				results[uncached_index_list[index]] = res;
				load_downloaded_thumbnail(uncached_index_list[index]);
			}));
			thread_network_session_list.perform(requests);
		}
		osTickCounterUpdate(&counter);
		// Util_log_save("thumb-dl", std::to_string((int) osTickCounterRead(&counter)) + " ms");
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

