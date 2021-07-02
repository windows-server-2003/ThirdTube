/*
	reference :
	https://github.com/pytube/pytube
*/

#include <string>
#include <regex>
#include "parser.hpp"
#include "json11/json11.hpp"

#ifdef _WIN32
#include <iostream> // <------------
#include <fstream> // <-------
#include <sstream> // <-------
#define debug(s) std::cerr << (s) << std::endl
#else
#include "headers.hpp"
#define debug(s) Util_log_save("yt-parser", (s));
#endif


using namespace json11;

static std::string get_text_from_object(Json json) {
	if (json["simpleText"] != Json()) return json["simpleText"].string_value();
	if (json["runs"] != Json()) {
		std::string res;
		for (auto i : json["runs"].array_items()) res += i["text"].string_value();
		return res;
	}
	return "";
}
// html can contain unnecessary garbage at the end of the actual json data
static Json to_json(const std::string &html, size_t start) {
	auto error_json = [&] (std::string error) {
		return Json::object{{{"Error", error}}};
	};
	if (start == html.size()) return error_json("empty suffix after 'ytInitialData'");
	if (html[start] != '{') return error_json("no '{' after 'ytInitialData'");
	size_t pos = start + 1;
	int level = 1;
	bool in_string = false;
	for (; pos < html.size(); pos++) {
		if (html[pos] == '"') in_string = !in_string;
		else if (in_string) {
			if (html[pos] == '\"') pos++;
		} else if (html[pos] == '{') level++;
		else if (html[pos] == '}') level--;
		if (level == 0) break;
	}
	if (level != 0) return error_json("the first '{' is never closed");
	
	std::string error;
	auto res = Json::parse(html.substr(start, pos - start + 1), error);
	if (error != "") return error_json(error);
	return res;
}

static Json get_initial_data(const std::string &html) {
	std::vector<std::regex> patterns = {
		std::regex(std::string("window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*")),
		std::regex(std::string("ytInitialData\\s*=\\s*"))
	};
	auto error_json = [&] (std::string error) {
		return Json::object{{{"Error", error}}};
	};
	for (auto pattern : patterns) {
		std::smatch match_res;
		if (std::regex_search(html, match_res, pattern)) {
			size_t start = match_res.suffix().first - html.begin();
			return to_json(html, start);
		}
	}
	return error_json("did not match any of the ytInitialData regexes");
}

static Json get_ytplayer_config(const std::string &html) {
	std::vector<std::regex> patterns = {
		std::regex(std::string("window\\[['\\\"]ytInitialPlayerResponse['\\\"]]\\s*=\\s*")),
		std::regex(std::string("ytInitialPlayerResponse\\s*=\\s*"))
	};
	auto error_json = [&] (std::string error) {
		return Json::object{{{"Error", error}}};
	};
	for (auto pattern : patterns) {
		std::smatch match_res;
		if (std::regex_search(html, match_res, pattern)) {
			size_t start = match_res.suffix().first - html.begin();
			return to_json(html, start);
		}
	}
	return error_json("did not match any of the ytInitialData regexes");
}

YouTubeVideoInfo parse_youtube_html(const std::string &html) {
	YouTubeVideoInfo res;
	Json initial_data = get_initial_data(html);
	Json primary_renderer, secondary_renderer;
	{
		auto contents = initial_data["contents"]["twoColumnWatchNextResults"]["results"]["results"]["contents"];
		for (auto content : contents.array_items()) {
			if (content["videoPrimaryInfoRenderer"] != Json()) primary_renderer = content["videoPrimaryInfoRenderer"];
			if (content["videoSecondaryInfoRenderer"] != Json()) secondary_renderer = content["videoSecondaryInfoRenderer"];
		}
	}
	Json player_config = get_ytplayer_config(html);
	{ // extract audio stream
		std::vector<Json> formats;
		for (auto i : player_config["streamingData"]["formats"].array_items()) formats.push_back(i);
		for (auto i : player_config["streamingData"]["adaptiveFormats"].array_items()) formats.push_back(i);
		std::vector<Json> audio_formats, video_formats;
		for (auto i : formats) {
			auto mime_type = i["mimeType"].string_value();
			if (mime_type.substr(0, 5) == "video") video_formats.push_back(i);
			else if (mime_type.substr(0, 5) == "audio") audio_formats.push_back(i);
			else {} // ???
		}
		int max_bitrate = -1;
		std::string best_audio_stream_url;
		for (auto i : audio_formats) {
			int cur_bitrate = i["bitrate"].int_value();
			if (max_bitrate < cur_bitrate) {
				max_bitrate = cur_bitrate;
				best_audio_stream_url = i["url"].string_value();
			}
		}
		res.audio_stream_url = best_audio_stream_url;
	}
	
	res.title = get_text_from_object(primary_renderer["title"]);
	res.author = get_text_from_object(secondary_renderer["owner"]["videoOwnerRenderer"]["title"]);
	debug(res.title);
	debug(res.author);
	debug(res.audio_stream_url);
	return res;
}
#ifdef _WIN32
int main() {
	std::ifstream file("test.html");
	std::stringstream sstream;
	sstream << file.rdbuf();
	parse_youtube_html(sstream.str());
	return 0;
}
#endif

