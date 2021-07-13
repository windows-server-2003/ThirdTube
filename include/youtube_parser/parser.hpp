#pragma once


struct YouTubeSearchResult {
	struct VideoInfo {
		std::string url;
		std::string title;
		std::string duration_text;
		std::string author;
		std::string thumbnail_url;
	};
	
	std::string error;
	int estimated_result_num;
	std::vector<VideoInfo> results;
};
YouTubeSearchResult parse_search(std::string url);

struct YouTubeVideoInfo {
	struct Channel {
		std::string name;
		std::string url;
		
		struct Icon {
			std::string url;
			int width;
			int height;
			std::vector<uint8_t> data;
		};
		Icon icon;
	};
	std::string error;
	std::string title;
	Channel author;
	std::string audio_stream_url;
	size_t audio_stream_len;
	std::string video_stream_url;
	size_t video_stream_len;
	std::string both_stream_url;
	size_t both_stream_len;
};
YouTubeVideoInfo parse_video_page(std::string url);
