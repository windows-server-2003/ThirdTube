#include "headers.hpp"
#include "system/util/history.hpp"
#include "system/util/util.hpp"
#include "ui/ui.hpp"
#include "json11/json11.hpp"

using namespace json11;

static std::vector<HistoryVideo> watch_history;
static bool lock_initialized = false;
static Handle resource_lock;

static void lock() {
	if (!lock_initialized) {
		lock_initialized = true;
		svcCreateMutex(&resource_lock, false);
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(resource_lock);
}

#define HISTORY_VERSION 0

void load_watch_history() {
	u64 file_size;
	Result_with_string result = Util_file_check_file_size("watch_history.json", DEF_MAIN_DIR, &file_size);
	if (result.code != 0) {
		Util_log_save("history/load" , "Util_file_check_file_size()..." + result.string + result.error_description, result.code);
		return;
	}
	
	char *buf = (char *) malloc(file_size + 1);
	
	u32 read_size;
	result = Util_file_load_from_file("watch_history.json", DEF_MAIN_DIR, (u8 *) buf, file_size, &read_size);
	Util_log_save("history/load" , "Util_file_load_from_file()..." + result.string + result.error_description, result.code);
	if (result.code == 0) {
		buf[read_size] = '\0';
		
		std::string error;
		Json data = Json::parse(buf, error);
		int version = data["version"] == Json() ? -1 : data["version"].int_value();
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
				if (!valid) Util_log_save("history/load", "invalid history item, ignoring...");
				else loaded_watch_history.push_back(cur_video);
			}
			std::sort(loaded_watch_history.begin(), loaded_watch_history.end(), [] (const HistoryVideo &i, const HistoryVideo &j) {
				return i.last_watch_time > j.last_watch_time;
			});
			lock();
			watch_history = loaded_watch_history;
			release();
			Util_log_save("history/load" , "loaded history(" + std::to_string(loaded_watch_history.size()) + " items)");
		} else {
			Util_log_save("history/load" , "failed to load history, json err:" + error);
		}
	}
	free(buf);
}
void save_watch_history() {
	lock();
	auto backup = watch_history;
	release();
	
	std::string data = 
		std::string() +
		"{\n" + 
			"\t\"version\": " + std::to_string(HISTORY_VERSION) + ",\n" +
			"\t\"history\": [\n";
	
	bool first = true;
	for (auto video : backup) {
		if (first) first = false;
		else data += ",";
		data += 
			std::string() +
			"\t\t{\n" +
				"\t\t\t\"id\": \"" + video.id + "\",\n" +
				"\t\t\t\"title\": \"" + video.title + "\",\n" +
				"\t\t\t\"author_name\": \"" + video.author_name + "\",\n" +
				"\t\t\t\"length\": \"" + video.length_text + "\",\n" +
				"\t\t\t\"my_view_count\": " + std::to_string(video.my_view_count) + ",\n" +
				"\t\t\t\"last_watch_time\": \"" + std::to_string(video.last_watch_time) + "\"\n" + // string value because we have to deal with u32 value which json11 doesn't support loading
			"\t\t}";
	}
	data += "\n";
	data += "\t]\n";
	data += "}\n";
	
	Result_with_string result = Util_file_save_to_file("watch_history.json", DEF_MAIN_DIR, (u8 *) data.c_str(), data.size(), true);
	Util_log_save("history/save", "Util_file_save_to_file()..." + result.string + result.error_description, result.code);
}
void add_watched_video(HistoryVideo video) {
	if (var_history_enabled) {
		lock();
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
		release();
	}
}
void history_erase_by_id(const std::string &id) {
	lock();
	std::vector<HistoryVideo> tmp_watch_history;
	for (auto video : watch_history) if (video.id != id) tmp_watch_history.push_back(video);
	watch_history = tmp_watch_history;
	release();
}
void history_erase_all() {
	lock();
	watch_history.clear();
	release();
}
std::vector<HistoryVideo> get_watch_history() {
	lock();
	std::vector<HistoryVideo> res = watch_history;
	release();
	return res;
}


