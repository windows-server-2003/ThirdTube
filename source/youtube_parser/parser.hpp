#pragma once


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
};
YouTubeSearchResult youtube_parse_search(std::string url);
// takes the previous result, returns the new result with both old items and new items
YouTubeSearchResult youtube_continue_search(const YouTubeSearchResult &prev_result);

struct YouTubeVideoDetail {
	std::string error;
	std::string title;
	YouTubeChannelSuccinct author;
	std::string audio_stream_url;
	size_t audio_stream_len;
	std::string video_stream_url;
	size_t video_stream_len;
	std::string both_stream_url;
	size_t both_stream_len;
};
YouTubeVideoDetail youtube_parse_video_page(std::string url);
