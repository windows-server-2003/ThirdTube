#pragma once
#include <vector>
#include <string>
#include <time.h>

struct HistoryVideo {
	std::string id;
	std::string title;
	std::vector<std::string> title_lines;
	std::string author_name;
	std::string length_text;
	int my_view_count;
	time_t last_watch_time;
	bool valid = true;
};
void load_watch_history();
void save_watch_history();
void add_watched_video(HistoryVideo video);
void history_erase_by_id(const std::string &id);
void history_erase_all();
std::vector<HistoryVideo> get_valid_watch_history();
