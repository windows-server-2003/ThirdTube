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
	} else if (request_type == LoadRequestType::VIDEO || request_type == LoadRequestType::VIDEO_SUGGESTION_CONTINUE || request_type == LoadRequestType::VIDEO_COMMENT_CONTINUE) {
		delete ((VideoRequestArg *) requests[(int) request_type]);
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
		new_wrapped_titles[i] = truncate_str(cur_str, arg.max_width, 2, arg.text_size_x, arg.text_size_y);
	}
	Util_log_save("wloader/search", "truncate end");
	
	
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	*arg.result = new_result;
	*arg.wrapped_titles = new_wrapped_titles;
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
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
	std::vector<std::vector<std::string> > wrapped_titles_add(new_result.results.size() - prev_result.results.size());
	for (size_t i = prev_result.results.size(); i < new_result.results.size(); i++) {
		std::string cur_str = new_result.results[i].type == YouTubeSearchResult::Item::VIDEO ? new_result.results[i].video.title : new_result.results[i].channel.name;
		wrapped_titles_add[i - prev_result.results.size()] = truncate_str(cur_str, arg.max_width, 2, arg.text_size_x, arg.text_size_y);
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
		*arg.result = new_result;
		arg.wrapped_titles->insert(arg.wrapped_titles->end(), wrapped_titles_add.begin(), wrapped_titles_add.end());
	}
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
	release();
}

static void load_search_channel(ChannelLoadRequestArg arg) {
	add_cpu_limit(25);
	YouTubeChannelDetail new_result = youtube_parse_channel_page(arg.url);
	remove_cpu_limit(25);
	
	// wrap and truncate here
	Util_log_save("wloader/channel", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(new_result.videos.size());
	for (size_t i = 0; i < new_result.videos.size(); i++)
		new_wrapped_titles[i] = truncate_str(new_result.videos[i].title, arg.max_width, 2, arg.text_size_x, arg.text_size_y);
	Util_log_save("wloader/channel", "truncate end");
	
	
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	*arg.result = new_result;
	*arg.wrapped_titles = new_wrapped_titles;
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
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
	std::vector<std::vector<std::string> > wrapped_titles_add(new_result.videos.size() - prev_result.videos.size());
	for (size_t i = prev_result.videos.size(); i < new_result.videos.size(); i++)
		wrapped_titles_add[i - prev_result.videos.size()] = truncate_str(new_result.videos[i].title, arg.max_width, 2, arg.text_size_x, arg.text_size_y);
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
		arg.wrapped_titles->insert(arg.wrapped_titles->end(), wrapped_titles_add.begin(), wrapped_titles_add.end());
	}
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
	release();
}

static void load_video(VideoRequestArg arg) {
	add_cpu_limit(25);
	YouTubeVideoDetail tmp_video_info = youtube_parse_video_page(arg.url);
	remove_cpu_limit(25);
	
	Util_log_save("wloader/video", "truncate start");
	// wrap main title
	float title_font_size;
	std::vector<std::string> title_lines = truncate_str(tmp_video_info.title, arg.title_max_width, 3, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
	if (title_lines.size() == 3) {
		title_lines = truncate_str(tmp_video_info.title, arg.title_max_width, 2, 0.5, 0.5);
		title_font_size = 0.5;
	} else title_font_size = MIDDLE_FONT_SIZE;
	// wrap description
	std::vector<std::string> description_lines;
	{
		auto &description = tmp_video_info.description;
		auto itr = description.begin();
		while (itr != description.end()) {
			auto next_itr = std::find(itr, description.end(), '\n');
			auto cur_lines = truncate_str(std::string(itr, next_itr), arg.description_max_width, 100, arg.description_text_size_x, arg.description_text_size_y);
			description_lines.insert(description_lines.end(), cur_lines.begin(), cur_lines.end());
			if (next_itr != description.end()) itr = std::next(next_itr);
			else break;
		}
	}
	// wrap suggestion titles
	std::vector<std::vector<std::string> > suggestion_titles_lines(tmp_video_info.suggestions.size());
	for (size_t i = 0; i < tmp_video_info.suggestions.size(); i++)
		suggestion_titles_lines[i] = truncate_str(tmp_video_info.suggestions[i].title, arg.suggestion_title_max_width, 2,
			arg.suggestion_title_text_size_x, arg.suggestion_title_text_size_y);
	Util_log_save("wloader/video", "truncate end");
	
	
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	*arg.result = tmp_video_info;
	*arg.description_lines = description_lines;
	*arg.suggestion_titles_lines = suggestion_titles_lines;
	*arg.title_lines = title_lines;
	*arg.title_font_size = title_font_size;
	
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
}
static void load_video_suggestion_continue(VideoRequestArg arg) {
	lock();
	if (requests[(int) LoadRequestType::VIDEO]) {
		release();
		return;
	}
	auto prev_result = *arg.result;
	release();
	
	add_cpu_limit(25);
	auto new_result = youtube_video_page_load_more_suggestions(prev_result);
	remove_cpu_limit(25);
	
	// wrap suggestion titles
	std::vector<std::vector<std::string> > suggestion_titles_lines_add(new_result.suggestions.size() - prev_result.suggestions.size());
	for (size_t i = prev_result.suggestions.size(); i < new_result.suggestions.size(); i++)
		suggestion_titles_lines_add[i - prev_result.suggestions.size()] = truncate_str(new_result.suggestions[i].title, arg.suggestion_title_max_width, 2,
			arg.suggestion_title_text_size_x, arg.suggestion_title_text_size_y);
	Util_log_save("wloader/video", "truncate end");
	
	
	lock();
	if (requests[(int) LoadRequestType::VIDEO]) {
		release();
		return;
	}
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") arg.result->error = new_result.error;
	else {
		*arg.result = new_result;
		arg.suggestion_titles_lines->insert(arg.suggestion_titles_lines->end(), suggestion_titles_lines_add.begin(), suggestion_titles_lines_add.end());
	}
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
	release();
}
#define COMMENT_MAX_LINE_NUM 40 // this limit exists due to performance reason (TODO : more efficient truncating)
static void load_video_comment_continue(VideoRequestArg arg) {
	lock();
	if (requests[(int) LoadRequestType::VIDEO]) {
		release();
		return;
	}
	auto prev_result = *arg.result;
	release();
	
	add_cpu_limit(25);
	auto new_result = youtube_video_page_load_more_comments(prev_result);
	remove_cpu_limit(25);
	
	// wrap comments
	Util_log_save("wloader/v-comment", "truncate end");
	std::vector<std::vector<std::string> > comments_lines_add(new_result.comments.size() - prev_result.comments.size());
	for (size_t i = prev_result.comments.size(); i < new_result.comments.size(); i++) {
		auto &cur_comment = new_result.comments[i].content;
		auto &result = comments_lines_add[i - prev_result.comments.size()];
		auto itr = cur_comment.begin();
		while (itr != cur_comment.end()) {
			if (result.size() >= COMMENT_MAX_LINE_NUM) break;
			auto next_itr = std::find(itr, cur_comment.end(), '\n');
			auto cur_lines = truncate_str(std::string(itr, next_itr), arg.comment_max_width, COMMENT_MAX_LINE_NUM - result.size(),
				arg.comment_text_size_x, arg.comment_text_size_y);
			result.insert(result.end(), cur_lines.begin(), cur_lines.end());
			
			if (next_itr != cur_comment.end()) itr = std::next(next_itr);
			else break;
		}
	}
	Util_log_save("wloader/v-comment", "truncate end");
	
	
	lock();
	if (requests[(int) LoadRequestType::VIDEO]) {
		release();
		return;
	}
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") arg.result->error = new_result.error;
	else {
		*arg.result = new_result;
		arg.comments_lines->insert(arg.comments_lines->end(), comments_lines_add.begin(), comments_lines_add.end());
	}
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
	release();
}



static bool should_be_running = true;
void webpage_loader_thread_exit_request() { should_be_running = false; }
void webpage_loader_thread_func(void* arg) {
	(void) arg;
	
	while (should_be_running) {
		lock();
		LoadRequestType next_type = LoadRequestType::NONE;
		// add here
		for (int i = 0; i < 7; i++) if (requests[i]) next_type = (LoadRequestType) i;
		
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
		if (next_type == LoadRequestType::VIDEO) load_video(*((VideoRequestArg *) requests[(int) LoadRequestType::VIDEO]));
		if (next_type == LoadRequestType::VIDEO_SUGGESTION_CONTINUE) load_video_suggestion_continue(*((VideoRequestArg *) requests[(int) LoadRequestType::VIDEO_SUGGESTION_CONTINUE]));
		if (next_type == LoadRequestType::VIDEO_COMMENT_CONTINUE) load_video_comment_continue(*((VideoRequestArg *) requests[(int) LoadRequestType::VIDEO_COMMENT_CONTINUE]));
		// add here
		
		lock();
		in_progress[(int) next_type] = false;
		delete_request(next_type);
		release();
	}
	
	threadExit(0);
}


/* General util function */

// truncate and wrap into at most `max_lines` lines so that each line fit in `max_width` if drawn with the size of `x_size` x `y_size`
std::vector<std::string> truncate_str(std::string input_str, int max_width, int max_lines, double x_size, double y_size) {
	static std::string input[1024 + 1];
	int n;
	Exfont_text_parse(input_str, &input[0], 1024, &n);
	
	std::vector<std::vector<std::string> > words; // each word is considered not separable
	for (int i = 0; i < n; i++) {
		bool seperate;
		if (!i) seperate = true;
		else {
			std::string last_char = words.back().back();
			seperate = last_char.size() != 1 || input[i].size() != 1 || last_char == " " || input[i] == " ";
		}
		if (seperate) words.push_back({input[i]});
		else words.back().push_back(input[i]);
	}
	
	int m = words.size();
	int head = 0;
	std::vector<std::string> res;
	for (int line = 0; line < max_lines; line++) {
		if (head >= m) break;
		
		int fit_word_num = 0;
		{ // binary search the number of words that fit in the line
			int l = 0;
			int r = std::min(50, m - head + 1);
			while (r - l > 1) {
				int m = l + ((r - l) >> 1);
				std::string query_text;
				for (int i = head; i < head + m; i++) for (auto j : words[i]) query_text += j;
				if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
				else r = m;
			}
			fit_word_num = l;
		}
		
		std::string cur_line;
		for (int i = head; i < head + fit_word_num; i++) for (auto j : words[i]) cur_line += j;
		bool force_fit = !fit_word_num || (line == max_lines - 1 && fit_word_num < m - head);
		if (force_fit) {
			std::vector<std::string> cur_word = words[head + fit_word_num];
			int l = 0;
			int r = cur_word.size();
			while (r - l > 1) { // binary search the number of characters that fit in the first line
				int m = l + ((r - l) >> 1);
				std::string query_text = cur_line;
				for (int i = 0; i < m; i++) query_text += cur_word[i];
				query_text += "...";
				if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
				else r = m;
			}
			for (int i = 0; i < l; i++) cur_line += cur_word[i];
			cur_line += "...";
			res.push_back(cur_line);
			head += fit_word_num + 1;
		} else {
			res.push_back(cur_line);
			head += fit_word_num;
		}
	}
	if (!res.size()) res = {""};
	return res;
}

