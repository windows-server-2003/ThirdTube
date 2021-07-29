#pragma once
#include <vector>
#include <string>

struct YouTubeChannelSuccinct {
	std::string name;
	std::string url;
	std::string icon_url;
};
struct YouTubeVideoSuccinct {
	std::string url;
	std::string title;
	std::string duration_text;
	std::string author;
	std::string thumbnail_url;
};


struct YouTubeSearchResult {
	struct Item {
		// TODO : use union or std::variant
		enum {
			VIDEO,
			CHANNEL
		} type;
		YouTubeVideoSuccinct video;
		YouTubeChannelSuccinct channel;
		Item () = default;
		Item (YouTubeVideoSuccinct video) : type(VIDEO), video(video) {}
		Item (YouTubeChannelSuccinct channel) : type(CHANNEL), channel(channel) {}
	};
	
	std::string error;
	int estimated_result_num;
	std::vector<Item> results;
	
	std::string continue_token;
	std::string continue_key;
	
	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
YouTubeSearchResult youtube_parse_search(std::string url);
// takes the previous result, returns the new result with both old items and new items
YouTubeSearchResult youtube_continue_search(const YouTubeSearchResult &prev_result);


struct YouTubeVideoDetail {
	std::string error;
	std::string title;
	std::string description;
	YouTubeChannelSuccinct author;
	std::string audio_stream_url;
	uint64_t audio_stream_len;
	std::string video_stream_url;
	uint64_t video_stream_len;
	std::string both_stream_url;
	uint64_t both_stream_len;
	
	std::vector<YouTubeVideoSuccinct> suggestions;
	struct Comment {
		YouTubeChannelSuccinct author;
		std::string content;
		std::string id;
		int reply_num;
	};
	std::vector<Comment> comments;
	
	std::string continue_key;
	std::string suggestions_continue_token;
	std::string comment_continue_token;
	int comment_continue_type; // -1 : unavailable, 0 : using watch_comments, 1 : using innertube
	bool comments_disabled;
	
	bool has_more_suggestions() { return continue_key != "" && suggestions_continue_token != ""; }
	bool has_more_comments() { return comment_continue_type != -1; }
};
// this function does not load comments; call youtube_video_page_load_more_comments() if necessary
YouTubeVideoDetail youtube_parse_video_page(std::string url);
YouTubeVideoDetail youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result);
YouTubeVideoDetail youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result);



struct YouTubeChannelDetail {
	std::string error;
	std::string name;
	std::string url;
	std::string icon_url;
	std::string banner_url;
	std::string description;
	std::vector<YouTubeVideoSuccinct> videos;
	
	std::string continue_token;
	std::string continue_key;
	
	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
YouTubeChannelDetail youtube_parse_channel_page(std::string url);
// takes the previous result, returns the new result with both old items and new items
YouTubeChannelDetail youtube_channel_page_continue(const YouTubeChannelDetail &prev_result);


void youtube_change_content_language(std::string language_code);
