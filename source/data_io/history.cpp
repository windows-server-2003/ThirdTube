#include "headers.hpp"
#include "history.hpp"
#include "util/util.hpp"
#include "ui/ui.hpp"
#include "rapidjson_wrapper.hpp"

using namespace rapidjson;

static std::vector<HistoryVideo> watch_history;
static Mutex resource_lock;

#define HISTORY_VERSION 0
#define HISTORY_FILE_NAME "watch_history.json"

void load_watch_history() {
	u64 file_size;
	Result_with_string result = Util_file_check_file_size(HISTORY_FILE_NAME, DEF_MAIN_DIR, &file_size);
	if (result.code != 0) {
		logger.error("history/load" , "Util_file_check_file_size()..." + result.string + result.error_description, result.code);
		return;
	}
	
	char *buf = (char *) malloc(file_size + 1);
	
	u32 read_size;
	result = Util_file_load_from_file(HISTORY_FILE_NAME, DEF_MAIN_DIR, (u8 *) buf, file_size, &read_size);
	logger.info("history/load" , "Util_file_load_from_file()..." + result.string + result.error_description, result.code);
	if (result.code == 0) {
		buf[read_size] = '\0';
		
		Document json_root;
		std::string error;
		RJson data = RJson::parse(json_root, buf, error);
		
		int version = data.has_key("version") ? data["version"].int_value() : -1;
		
		if (version >= 0) {
			std::vector<HistoryVideo> loaded_watch_history;
			for (auto video : data["history"].array_items()) {
				HistoryVideo cur_video;
				cur_video.id = video["id"].string_value();
				cur_video.title = video["title"].string_value();
				cur_video.title_lines = truncate_str(cur_video.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - 6, 2, 0.5, 0.5);
				cur_video.author_name = video["author_name"].string_value();
				cur_video.length_text = video["length"].string_value();
				cur_video.my_view_count = video["my_view_count"].int_value();
				{
					auto str = video["last_watch_time"].string_value();
					char *end;
					cur_video.last_watch_time = strtoll(str.c_str(), &end, 10);
				}
				// validation
				bool valid = youtube_is_valid_video_id(cur_video.id);
				if (!valid) logger.caution("history/load", "invalid history item, ignoring...");
				else loaded_watch_history.push_back(cur_video);
			}
			std::sort(loaded_watch_history.begin(), loaded_watch_history.end(), [] (const HistoryVideo &i, const HistoryVideo &j) {
				return i.last_watch_time > j.last_watch_time;
			});
			resource_lock.lock();
			watch_history = loaded_watch_history;
			resource_lock.unlock();
			logger.info("history/load" , "loaded history(" + std::to_string(loaded_watch_history.size()) + " items)");
		} else logger.error("history/load" , "failed to load history, json err:" + error);
		
	}
	free(buf);
}
void save_watch_history() {
	resource_lock.lock();
	auto backup = watch_history;
	resource_lock.unlock();
	
	Document json_root;
	auto &allocator = json_root.GetAllocator();
	
	json_root.SetObject();
	json_root.AddMember("version", std::to_string(HISTORY_VERSION), allocator);
	
	Value history(kArrayType);
	for (auto video : backup) {
		Value cur_json(kObjectType);
		cur_json.AddMember("id", video.id, allocator);
		cur_json.AddMember("title", video.title, allocator);
		cur_json.AddMember("author_name", video.author_name, allocator);
		cur_json.AddMember("my_view_count", Value(video.my_view_count), allocator);
		cur_json.AddMember("last_watch_time", std::to_string(video.last_watch_time), allocator); // string value because we have to deal with u32 value, not int
		history.PushBack(cur_json, allocator);
	}
	json_root.AddMember("history", history, allocator);
	
	std::string data = RJson(json_root).dump();
	
	Result_with_string result = Util_file_save_to_file(HISTORY_FILE_NAME, DEF_MAIN_DIR, (u8 *) data.c_str(), data.size(), true);
	logger.info("history/save", "Util_file_save_to_file()..." + result.string + result.error_description, result.code);
}
void add_watched_video(HistoryVideo video) {
	if (var_history_enabled) {
		resource_lock.lock();
		bool found = false;
		for (auto &i : watch_history) if (i.id == video.id) {
			i.my_view_count++;
			i.last_watch_time = video.last_watch_time;
			found = true;
		}
		if (!found) {
			video.title_lines = truncate_str(video.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - 6, 2, 0.5, 0.5);
			watch_history.push_back(video);
		}
		std::sort(watch_history.begin(), watch_history.end(), [] (const HistoryVideo &i, const HistoryVideo &j) {
			return i.last_watch_time > j.last_watch_time;
		});
		resource_lock.unlock();
	}
}
void history_erase_by_id(const std::string &id) {
	resource_lock.lock();
	std::vector<HistoryVideo> tmp_watch_history;
	for (auto video : watch_history) if (video.id != id) tmp_watch_history.push_back(video);
	watch_history = tmp_watch_history;
	resource_lock.unlock();
}
void history_erase_all() {
	resource_lock.lock();
	watch_history.clear();
	resource_lock.unlock();
}
std::vector<HistoryVideo> get_watch_history() {
	resource_lock.lock();
	std::vector<HistoryVideo> res = watch_history;
	resource_lock.unlock();
	return res;
}


