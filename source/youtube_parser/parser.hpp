#pragma once

struct YoutubeChannel {
	std::string name;
	std::string url;
	std::string icon_url;
};
struct YouTubeVideoInfo {
	std::string error;
	std::string title;
	YoutubeChannel author;
	std::string audio_stream_url;
	size_t audio_stream_len;
	std::string video_stream_url;
	size_t video_stream_len;
};
YouTubeVideoInfo parse_youtube_html(std::string url);
