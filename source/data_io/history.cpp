#include "headers.hpp"
#include "history.hpp"
#include "util/util.hpp"
#include "ui/ui.hpp"
#include "rapidjson_wrapper.hpp"

using namespace rapidjson;

static std::vector<HistoryVideo> watch_history;
static Mutex resource_lock;

#define HISTORY_VERSION 0
#define HISTORY_FILE_PATH (DEF_MAIN_DIR + "watch_history.json")
#define HISTORY_FILE_TMP_PATH (DEF_MAIN_DIR + "watch_history_tmp.json")

static AtomicFileIO atomic_io(HISTORY_FILE_PATH, HISTORY_FILE_TMP_PATH);

void load_watch_history() {
	auto tmp = atomic_io.load([] (const std::string &data) {
		Document json_root;
		std::string error;
		RJson data_json = RJson::parse(json_root, data.c_str(), error);
		
		int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;
		return version >= 0;
	});
	Result_with_string result = tmp.first;
	std::string data = tmp.second;
	if (result.code != 0) {
		logger.error("history/load", result.string + result.error_description + " " + std::to_string(result.code));
		return;
	}
	
	Document json_root;
	std::string error;
	RJson data_json = RJson::parse(json_root, data.c_str(), error);
	
	int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;
	
	std::vector<HistoryVideo> loaded_watch_history;
	if (version >= 0) {
		for (auto video : data_json["history"].array_items()) {
			HistoryVideo cur_video;
			cur_video.id = video["id"].string_value();
			cur_video.title = video["title"].string_value();
			cur_video.title_lines = truncate_str(cur_video.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - 6, 2, 0.5, 0.5);
			cur_video.author_name = video["author_name"].string_value();
			cur_video.length_text = video["length"].string_value();
			cur_video.my_view_count = video["my_view_count"].int_value();
			cur_video.last_watch_time = strtoll(video["last_watch_time"].string_value().c_str(), NULL, 10);
			cur_video.valid = youtube_is_valid_video_id(cur_video.id);
			if (!cur_video.valid) logger.caution("history/load", "invalid history item : " + cur_video.title);
			
			loaded_watch_history.push_back(cur_video);
		}
		std::sort(loaded_watch_history.begin(), loaded_watch_history.end(), [] (const HistoryVideo &i, const HistoryVideo &j) {
			return i.last_watch_time > j.last_watch_time;
		});
	} else {
		logger.error("history/load", "json err : " + data.substr(0, 40));
		return;
	}
	
	resource_lock.lock();
	watch_history = loaded_watch_history;
	resource_lock.unlock();
	logger.info("history/load" , "loaded history(" + std::to_string(loaded_watch_history.size()) + " items)");
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
	
	auto result = atomic_io.save(data);
	if (result.code != 0) logger.warning("history/save", result.string + result.error_description, result.code);
	else logger.info("history/save", "history saved.");
}
void add_watched_video(HistoryVideo video) {
	if (var_history_enabled) {
		video.title_lines = truncate_str(video.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - 6, 2, 0.5, 0.5);
		resource_lock.lock();
		bool found = false;
		for (auto &i : watch_history) if (i.id == video.id) {
			int my_view_count = i.my_view_count + 1;
			i = video;
			i.my_view_count = my_view_count;
			found = true;
		}
		if (!found) {
			video.my_view_count = 1;
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
// ------------------------------------------------
std::vector<HistoryVideo> get_valid_watch_history() {
	resource_lock.lock();
	std::vector<HistoryVideo> res;
	for (auto &video : watch_history) if (video.valid) res.push_back(video);
	resource_lock.unlock();
	return res;
}


