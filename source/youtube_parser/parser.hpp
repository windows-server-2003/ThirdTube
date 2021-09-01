#pragma once
#include <vector>
#include <string>

struct YouTubeChannelSuccinct {
	std::string name;
	std::string url;
	std::string icon_url;
	std::string subscribers;
	std::string video_num;
};
struct YouTubeVideoSuccinct {
	std::string url;
	std::string title;
	std::string duration_text;
	std::string publish_date;
	std::string views_str;
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
	std::string video_stream_url;
	std::string both_stream_url;
	int duration_ms;
	bool is_livestream;
	enum class LivestreamType {
		PREMIERE,
		LIVESTREAM,
	};
	LivestreamType livestream_type;
	bool is_upcoming;
	std::string playability_status;
	std::string playability_reason;
	int stream_fragment_len; // used only for livestreams
	std::string like_count_str;
	std::string dislike_count_str;
	std::string publish_date;
	std::string views_str;
	
	std::vector<YouTubeVideoSuccinct> suggestions;
	struct Comment {
		YouTubeChannelSuccinct author;
		std::string content;
		std::string id;
		
		int reply_num;
		std::vector<Comment> replies;
		std::string continue_key; // innertube key, equals to YouTubeChannelDetail.continue_key
		std::string replies_continue_token;
		bool has_more_replies() const { return replies_continue_token != ""; }
	};
	std::vector<Comment> comments;
	
	std::string continue_key; // innertube key
	std::string suggestions_continue_token;
	std::string comment_continue_token;
	int comment_continue_type; // -1 : unavailable, 0 : using watch_comments, 1 : using innertube
	bool comments_disabled;
	
	bool has_more_suggestions() const { return continue_key != "" && suggestions_continue_token != ""; }
	bool has_more_comments() const { return comment_continue_type != -1; }
	bool needs_timestamp_adjusting() const { return is_livestream && livestream_type == LivestreamType::PREMIERE; }
	bool is_playable() const { return playability_status == "OK" && (both_stream_url != "" || (audio_stream_url != "" && video_stream_url != "")); }
};
// this function does not load comments; call youtube_video_page_load_more_comments() if necessary
YouTubeVideoDetail youtube_parse_video_page(std::string url);
YouTubeVideoDetail youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result);
YouTubeVideoDetail youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result);
YouTubeVideoDetail::Comment youtube_video_page_load_more_replies(const YouTubeVideoDetail::Comment &comment);



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
