#include "headers.hpp"


static Handle resource_lock;
static bool resource_lock_initialized = false;
static void *requests[100] = { NULL };
static bool in_progress[100] = { false };

static void lock() {
	if (!resource_lock_initialized) {
		svcCreateMutex(&resource_lock, false);
		resource_lock_initialized = true;
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(resource_lock);
}

static void delete_request(LoadRequestType request_type) {
	if (request_type == LoadRequestType::SEARCH || request_type == LoadRequestType::SEARCH_CONTINUE) {
		delete ((SearchRequestArg *) requests[(int) request_type]);
	} else if (request_type == LoadRequestType::CHANNEL || request_type == LoadRequestType::CHANNEL_CONTINUE) {
		delete ((ChannelLoadRequestArg *) requests[(int) request_type]);
	}
	// add here
	requests[(int) request_type] = NULL;
}

bool request_webpage_loading(LoadRequestType request_type, void *arg) {
	lock();
	bool res;
	if (!requests[(int) request_type]) requests[(int) request_type] = arg, res = true;
	else res = false;
	release();
	return res;
}

void cancel_webpage_loading(LoadRequestType request_type) {
	lock();
	if (requests[(int) request_type] && !in_progress[(int) request_type]) delete_request(request_type);
	release();
}

bool is_webpage_loading_requested(LoadRequestType request_type) {
	return requests[(int) request_type] != NULL;
}


/*
	The functions below here run in webpage loader thread
	--------------------------------------------------------------------------------
	--------------------------------------------------------------------------------
	--------------------------------------------------------------------------------
*/


// truncate and wrap into two lines if necessary
static std::vector<std::string> truncate_str(std::string input_str, int max_width, double x_size, double y_size) {
	if (input_str == "") return {""};
	
	std::vector<std::string> input(128);
	{
		int out_num;
		Exfont_text_parse(input_str, &input[0], 128, &out_num);
		input.resize(out_num);
	}
	if (!input.size()) return {""};
	std::vector<std::vector<std::string> > words; // each word is considered not separable
	for (size_t i = 0; i < input.size(); i++) {
		bool seperate;
		if (!i) seperate = true;
		else {
			std::string last_char = words.back().back();
			seperate = last_char.size() != 1 || input[i].size() != 1 || last_char == " " || input[i] == " ";
		}
		if (seperate) words.push_back({input[i]});
		else words.back().push_back(input[i]);
	}
	
	int n = words.size();
	int first_line_word_num = 0;
	{ // binary search the number of words that fit in the first line
		int l = 0;
		int r = n + 1;
		while (r - l > 1) {
			int m = l + ((r - l) >> 1);
			std::string query_text;
			for (int i = 0; i < m; i++) for (auto j : words[i]) query_text += j;
			if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
			else r = m;
		}
		first_line_word_num = l;
	}
	
	if (!first_line_word_num) { // can't even accommodate the first word -> partially display the word and add "..."
		std::vector<std::string> first_word = words[0];
		int l = 0;
		int r = first_word.size();
		while (r - l > 1) { // binary search the number of characters that fit in the first line
			int m = l + ((r - l) >> 1);
			std::string query_text;
			for (int i = 0; i < m; i++) query_text += first_word[i];
			query_text += "...";
			if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
			else r = m;
		}
		std::string first_line;
		for (int i = 0; i < l; i++) first_line += first_word[i];
		first_line += "...";
		return {first_line};
	} else {
		std::string first_line;
		for (int i = 0; i < first_line_word_num; i++) for (auto j : words[i]) first_line += j;
		words.erase(words.begin(), words.begin() + first_line_word_num);
		if (!words.size()) return {first_line}; // the first line accommodated the entire string
		std::vector<std::string> remaining_str; // ignore the word unit from here
		for (auto i : words) for (auto j : i) remaining_str.push_back(j);
		
		// check if the entire remaining string fit in the second line
		{
			std::string tmp_str;
			for (auto i : remaining_str) tmp_str += i;
			if (Draw_get_width(tmp_str, x_size, y_size) <= max_width) return {first_line, tmp_str};
		}
		// binary search the number of words that fit in the second line with "..."
		int l = 0;
		int r = remaining_str.size();
		while (r - l > 1) {
			int m = l + ((r - l) >> 1);
			std::string query_text;
			for (int i = 0; i < m; i++) query_text += remaining_str[i];
			query_text += "...";
			if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
			else r = m;
		}
		std::string second_line;
		for (int i = 0; i < l; i++) second_line += remaining_str[i];
		second_line += "...";
		return {first_line, second_line};
	}
}

static void load_search(SearchRequestArg arg) {
	std::string search_url = "https://m.youtube.com/results?search_query=";
	for (auto c : arg.search_word) {
		search_url.push_back('%');
		search_url.push_back("0123456789ABCDEF"[(u8) c / 16]);
		search_url.push_back("0123456789ABCDEF"[(u8) c % 16]);
	}
	add_cpu_limit(25);
	YouTubeSearchResult new_result = youtube_parse_search(search_url);
	remove_cpu_limit(25);
	
	// wrap and truncate here
	Util_log_save("wloader/search", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(new_result.results.size());
	for (size_t i = 0; i < new_result.results.size(); i++) {
		std::string cur_str = new_result.results[i].type == YouTubeSearchResult::Item::VIDEO ? new_result.results[i].video.title : new_result.results[i].channel.name;
		new_wrapped_titles[i] = truncate_str(cur_str, arg.max_width, arg.text_size_x, arg.text_size_y);
	}
	Util_log_save("wloader/search", "truncate end");
	
	
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	*arg.result = new_result;
	*arg.wrapped_titles = new_wrapped_titles;
	for (auto i : new_result.results) {
		if (i.type == YouTubeSearchResult::Item::VIDEO) request_thumbnail(i.video.thumbnail_url);
		else if (i.type == YouTubeSearchResult::Item::CHANNEL) request_thumbnail(i.channel.icon_url);
	}
	svcReleaseMutex(arg.lock);
	
	if (arg.on_load_complete) arg.on_load_complete();
}
static void load_search_continue(SearchRequestArg arg) {
	lock();
	if (requests[(int) LoadRequestType::SEARCH]) {
		release();
		return;
	}
	auto prev_result = *arg.result;
	release();
	
	auto new_result = youtube_continue_search(prev_result);
	
	Util_log_save("wloader/search-c", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(new_result.results.size());
	for (size_t i = 0; i < new_result.results.size(); i++) {
		std::string cur_str = new_result.results[i].type == YouTubeSearchResult::Item::VIDEO ? new_result.results[i].video.title : new_result.results[i].channel.name;
		new_wrapped_titles[i] = truncate_str(cur_str, arg.max_width, arg.text_size_x, arg.text_size_y);
	}
	Util_log_save("wloader/search-c", "truncate end");
	
	
	lock();
	if (requests[(int) LoadRequestType::SEARCH]) {
		release();
		return;
	}
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") arg.result->error = new_result.error;
	else {
		for (size_t i = arg.result->results.size(); i < new_result.results.size(); i++) {
			if (new_result.results[i].type == YouTubeSearchResult::Item::VIDEO) request_thumbnail(new_result.results[i].video.thumbnail_url);
			else if (new_result.results[i].type == YouTubeSearchResult::Item::CHANNEL) request_thumbnail(new_result.results[i].channel.icon_url);
		}
		*arg.result = new_result;
		*arg.wrapped_titles = new_wrapped_titles;
	}
	svcReleaseMutex(arg.lock);
	release();
	
	if (arg.on_load_complete) arg.on_load_complete();
}

static void load_search_channel(ChannelLoadRequestArg arg) {
	add_cpu_limit(25);
	YouTubeChannelDetail new_result = youtube_parse_channel_page(arg.url);
	remove_cpu_limit(25);
	
	// wrap and truncate here
	Util_log_save("wloader/channel", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(new_result.videos.size());
	for (size_t i = 0; i < new_result.videos.size(); i++)
		new_wrapped_titles[i] = truncate_str(new_result.videos[i].title, arg.max_width, arg.text_size_x, arg.text_size_y);
	Util_log_save("wloader/channel", "truncate end");
	
	
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	*arg.result = new_result;
	*arg.wrapped_titles = new_wrapped_titles;
	for (auto i : new_result.videos)
		request_thumbnail(i.thumbnail_url);
	if (new_result.banner_url != "") request_thumbnail(new_result.banner_url);
	if (new_result.icon_url != "") request_thumbnail(new_result.icon_url);
	svcReleaseMutex(arg.lock);
	
	if (arg.on_load_complete) arg.on_load_complete();
}
static void load_search_channel_continue(ChannelLoadRequestArg arg) {
	lock();
	if (requests[(int) LoadRequestType::CHANNEL]) {
		release();
		return;
	}
	auto prev_result = *arg.result;
	release();
	
	auto new_result = youtube_channel_page_continue(prev_result);
	
	Util_log_save("wloader/channel-c", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(new_result.videos.size());
	for (size_t i = 0; i < new_result.videos.size(); i++)
		new_wrapped_titles[i] = truncate_str(new_result.videos[i].title, arg.max_width, arg.text_size_x, arg.text_size_y);
	Util_log_save("wloader/channel-c", "truncate end");
	
	
	lock();
	if (requests[(int) LoadRequestType::CHANNEL]) {
		release();
		return;
	}
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") arg.result->error = new_result.error;
	else {
		*arg.result = new_result;
		*arg.wrapped_titles = new_wrapped_titles;
		for (auto i : new_result.videos)
			request_thumbnail(i.thumbnail_url);
		if (new_result.banner_url != "") request_thumbnail(new_result.banner_url);
		if (new_result.icon_url != "") request_thumbnail(new_result.icon_url);
	}
	svcReleaseMutex(arg.lock);
	release();
	
	if (arg.on_load_complete) arg.on_load_complete();
	
}

static bool should_be_running = true;
void webpage_loader_thread_exit_request() { should_be_running = false; }
void webpage_loader_thread_func(void* arg) {
	(void) arg;
	
	while (should_be_running) {
		lock();
		LoadRequestType next_type = LoadRequestType::NONE;
		if (requests[(int) LoadRequestType::SEARCH]) next_type = LoadRequestType::SEARCH;
		else if (requests[(int) LoadRequestType::SEARCH_CONTINUE]) next_type = LoadRequestType::SEARCH_CONTINUE;
		else if (requests[(int) LoadRequestType::CHANNEL]) next_type = LoadRequestType::CHANNEL;
		else if (requests[(int) LoadRequestType::CHANNEL_CONTINUE]) next_type = LoadRequestType::CHANNEL_CONTINUE;
		// add here
		
		if (next_type != LoadRequestType::NONE) in_progress[(int) next_type] = true;
		release();
		if (next_type == LoadRequestType::NONE) {
			usleep(50000);
			continue;
		}
		
		if (next_type == LoadRequestType::SEARCH) load_search(*((SearchRequestArg *) requests[(int) LoadRequestType::SEARCH]));
		if (next_type == LoadRequestType::SEARCH_CONTINUE) load_search_continue(*((SearchRequestArg *) requests[(int) LoadRequestType::SEARCH_CONTINUE]));
		if (next_type == LoadRequestType::CHANNEL) load_search_channel(*((ChannelLoadRequestArg *) requests[(int) LoadRequestType::CHANNEL]));
		if (next_type == LoadRequestType::CHANNEL_CONTINUE) load_search_channel_continue(*((ChannelLoadRequestArg *) requests[(int) LoadRequestType::CHANNEL_CONTINUE]));
		// add here
		
		lock();
		in_progress[(int) next_type] = false;
		delete_request(next_type);
		release();
	}
	
	threadExit(0);
}
