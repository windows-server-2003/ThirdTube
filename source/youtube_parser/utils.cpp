#include "parser.hpp"
#include "internal_common.hpp"

std::string youtube_get_video_id_by_url(const std::string &url) {
	auto pos = url.find("?v=");
	if (pos == std::string::npos) pos = url.find("&v=");
	if (pos != std::string::npos) {
		std::string res = url.substr(pos + 3, 11);
		return youtube_is_valid_video_id(res) ? res : "";
	}
	return "";
}
std::string youtube_get_playlist_id_by_url(const std::string &url) {
	auto pos = url.find("?list=");
	if (pos == std::string::npos) pos = url.find("&list=");
	if (pos != std::string::npos) {
		pos += 6;
		std::string res;
		for (size_t i = pos; i < url.size() && url[i] != '&'; i++) res.push_back(url[i]);
		return res;
	}
	return "";
}
std::string youtube_get_video_thumbnail_url_by_id(const std::string &id) {
	return "https://i.ytimg.com/vi/" + id + "/default.jpg";
}
std::string youtube_get_video_url_by_id(const std::string &id) {
	return "https://m.youtube.com/watch?v=" + id;
}
std::string get_video_id_from_thumbnail_url(const std::string &url) {
	auto pos = url.find("i.ytimg.com/vi/");
	if (pos == std::string::npos) return "";
	pos += std::string("i.ytimg.com/vi/").size();
	std::string res;
	while (pos < url.size() && url[pos] != '/') res.push_back(url[pos++]);
	return res;
}
bool youtube_is_valid_video_id(const std::string &id) {
	for (auto c : id) if (!isalnum(c) && c != '-' && c != '_') return false;
	if (id.size() != 11) return false;
	return true;
}
bool is_youtube_url(const std::string &url) {
	std::vector<std::string> patterns = {
		"https://m.youtube.com/",
		"https://www.youtube.com/"
	};
	for (auto pattern : patterns) if (starts_with(url, pattern, 0)) return true;
	return false;
}
bool is_youtube_thumbnail_url(const std::string &url) {
	std::vector<std::string> patterns = {
		"https://i.ytimg.com/vi/",
		"https://yt3.ggpht.com/"
	};
	for (auto pattern : patterns) if (starts_with(url, pattern, 0)) return true;
	return false;
}
YouTubePageType youtube_get_page_type(std::string url) {
	url = convert_url_to_mobile(url);
	if (starts_with(url, "https://m.youtube.com/watch?", 0)) return YouTubePageType::VIDEO;
	if (starts_with(url, "https://m.youtube.com/user/", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/channel/", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/c/", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/@", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/results?", 0)) return YouTubePageType::SEARCH;
	return YouTubePageType::INVALID;
}

