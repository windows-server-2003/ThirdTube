#pragma once

struct YouTubeVideoInfo {
	std::string title;
	std::string author;
	std::string audio_stream_url;
};
YouTubeVideoInfo parse_youtube_html(const std::string &html);
