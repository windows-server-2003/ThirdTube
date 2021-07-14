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

static Handle resource_lock;
static bool lock_initialized = false;
static std::map<std::string, int> thumbnail_request;
static std::map<std::string, LoadedThumbnail> thumbnail_cache;
static std::queue<std::string> load_queue;

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

void request_thumbnail(std::string url) {
	lock();
	load_queue.push(url);
	thumbnail_request[url]++;
	release();
}
void cancel_request_thumbnail(std::string url) {
	lock();
	if (!--thumbnail_request[url]) {
		thumbnail_request.erase(url);
		if (thumbnail_cache.count(url)) {
			Draw_c2d_image_free(thumbnail_cache[url].data);
			thumbnail_cache.erase(url);
		}
	}
	release();
}
bool thumbnail_available(std::string url) {
	lock();
	bool res = thumbnail_request.count(url) && thumbnail_cache.count(url);
	release();
	return res;
}
bool draw_thumbnail(std::string url, int x_offset, int y_offset, int x_len, int y_len) {
	bool res;
	lock();
	if (thumbnail_cache.count(url)) {
		LoadedThumbnail thumbnail = thumbnail_cache[url];
		Draw_texture(thumbnail.data.c2d, x_offset, y_offset, x_len, y_len);
		res = true;
	} else res = false;
	release();
	return res;
}


static std::vector<u8> http_get(const std::string &url) {
	constexpr int BLOCK = 0x40000; // 256 KB
	add_cpu_limit(30);
	Util_log_save("thumb-dl", "accessing...");
	// use mobile version of User-Agent for smaller webpage (and the whole parser is designed to parse the mobile version)
	auto network_res = access_http_get(url, {{"User-Agent", "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36"}});
	std::vector<u8> res;
	if (network_res.first == "") {
		Util_log_save("thumb-dl", "downloading...");
		std::vector<u8> buffer(BLOCK);
		while (1) {
			u32 len_read;
			Result ret = httpcDownloadData(&network_res.second, &buffer[0], BLOCK, &len_read);
			res.insert(res.end(), buffer.begin(), buffer.begin() + len_read);
			if (ret != (s32) HTTPC_RESULTCODE_DOWNLOADPENDING) break;
		}
		
		httpcCloseContext(&network_res.second);
	} else Util_log_save("thumb-dl", "failed accessing : " + network_res.first);
	
	Util_log_save("thumb-dl", "download ok");
	remove_cpu_limit(30);
	return res;
}


static bool should_be_running = true;
void thumbnail_downloader_thread_func(void *arg) {
	while (should_be_running) {
		bool need_download = false;
		std::string next_url;
		lock();
		while (load_queue.size()) {
			next_url = load_queue.front();
			load_queue.pop();
			if (thumbnail_request.count(next_url) && !thumbnail_cache.count(next_url)) {
				need_download = true;
				break;
			}
		}
		release();
		
		if (!need_download) {
			usleep(50000);
			continue;
		}
		auto encoded_data = http_get(next_url);
		
		int w, h;
		u8 *decoded_data = Image_decode(&encoded_data[0], encoded_data.size(), &w, &h);
		if (decoded_data) {
			// for video thumbnail, crop to 16:9
			if (next_url.find("https://i.ytimg.com/vi/") != std::string::npos && h > w * 9 / 16 + 1) {
				int new_h = w * 9 / 16 + 1;
				int vertical_offset = (h - new_h) / 2;
				memmove(decoded_data, decoded_data + vertical_offset * w * 2, new_h * w * 2);
				h = new_h;
			}
			
			Image_data result_image;
			int texture_size = std::max((w + 31) / 32 * 32, (h + 31) / 32 * 32);
			
			Result_with_string result;
			result = Draw_c2d_image_init(&result_image, texture_size, texture_size, GPU_RGB565);
			if (result.code != 0) {
				Util_log_save("thumb-dl", "out of linearmem");
			} else {
				result = Draw_set_texture_data(&result_image, decoded_data, w, h, texture_size, texture_size, GPU_RGB565);
				if (result.code != 0) {
					Util_log_save("thumb-dl", "Draw_set_texture_data() failed");
				} else {
					lock();
					if (thumbnail_request.count(next_url)) { // in case the request is cancelled while downloading
						thumbnail_cache[next_url] = {w, h, texture_size, texture_size, result_image};
						Util_log_save("thumb-dl", "successfully loaded thumbnail : " + next_url);
					}
					release();
				}
			}
			free(decoded_data);
			decoded_data = NULL;
		} else Util_log_save("thumb-dl", "Image_decode() failed");
	}
	
	lock();
	for (auto i : thumbnail_cache) Draw_c2d_image_free(i.second.data);
	thumbnail_cache.clear();
	release();
	
	Util_log_save("thumb-dl", "Thread exit.");
	threadExit(0);
}
void thumbnail_downloader_thread_exit_request() {
	should_be_running = false;
}

