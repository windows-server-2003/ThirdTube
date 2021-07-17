#pragma once
#include "types.hpp"
#include "youtube_parser/parser.hpp"

// does YouTube searching and channel loading (and more in the future)
// we don't want to increase the number of threads every time we add a new feature, so this single thread handles everything
// cannot handle multiple requests of the same type at the same time

enum class LoadRequestType {
	SEARCH,
	SEARCH_CONTINUE,
	CHANNEL,
	CHANNEL_CONTINUE,
	VIDEO,
	VIDEO_SUGGESTION_CONTINUE,
	VIDEO_COMMENT_CONTINUE,
	NONE // used internally
};
struct SearchRequestArg {
	Handle lock;
	YouTubeSearchResult *result;
	std::string search_word; // not used for SEARCH_CONTINUE
	// for truncating
	int max_width;
	float text_size_x;
	float text_size_y;
	std::vector<std::vector<std::string> > *wrapped_titles;
	
	void (*on_load_complete) (void);
};
struct ChannelLoadRequestArg {
	Handle lock;
	YouTubeChannelDetail *result;
	std::string url; // not used for CHANNEL_CONTINUE
	
	// for truncating
	int max_width;
	float text_size_x;
	float text_size_y;
	std::vector<std::vector<std::string> > *wrapped_titles;
	
	void (*on_load_complete) (void);
};
struct VideoRequestArg {
	Handle lock;
	YouTubeVideoDetail *result;
	std::string url;
	
	// for truncating
	int title_max_width;
	int description_max_width;
	float description_text_size_x;
	float description_text_size_y;
	int suggestion_title_max_width;
	float suggestion_title_text_size_x;
	float suggestion_title_text_size_y;
	int comment_max_width;
	float comment_text_size_x;
	float comment_text_size_y;
	
	std::vector<std::string> *title_lines;
	float *title_font_size;
	std::vector<std::string> *description_lines;
	std::vector<std::vector<std::string> > *suggestion_titles_lines;
	std::vector<std::vector<std::string> > *comments_lines;
	
	void (*on_load_complete) (void);
};

// Caller should pass a new-ed *Arg instance as 'arg'. It will be deleted internally
bool request_webpage_loading(LoadRequestType request_type, void *arg);

void cancel_webpage_loading(LoadRequestType request_type);

bool is_webpage_loading_requested(LoadRequestType request_type);

void webpage_loader_thread_func(void *arg);
void webpage_loader_thread_exit_request();


std::vector<std::string> truncate_str(std::string input_str, int max_width, int max_lines, double x_size, double y_size);
