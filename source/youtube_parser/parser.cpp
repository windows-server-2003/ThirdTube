/*
	reference :
	https://github.com/pytube/pytube
*/

#include <string>
#include <regex>
#include "parser.hpp"
#include "json11/json11.hpp"
#include "cipher.hpp"

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

static std::string url_decode(std::string input) {
	std::string res;
	for (size_t i = 0; i < input.size(); i++) {
		if (input[i] == '%') {
			res.push_back((char) stoi(input.substr(i + 1, 2), nullptr, 16));
			i += 2;
		} else res.push_back(input[i]);
	}
	return res;
}

// parse something like 'abc=def&ghi=jkl&lmn=opq'
static std::map<std::string, std::string> parse_parameters(std::string input) {
	size_t start = 0;
	std::map<std::string, std::string> res;
	for (size_t i = 0; i <= input.size(); i++) {
		if (i == input.size() || input[i] == '&') {
			std::string first;
			std::string second;
			bool is_second = false;
			for (size_t j = start; j < i; j++) {
				if (input[j] == '=') is_second = true;
				else if (is_second) second.push_back(input[j]);
				else first.push_back(input[j]);
			}
			first = url_decode(first);
			second = url_decode(second);
			res[first] = second;
			start = i + 1;
		}
	}
	return res;
}

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
	while (start < html.size() && html[start] == ' ') start++;
	if (start >= html.size()) return error_json("empty suffix after 'ytInitialData'");
	if (html[start] == '\'') {
		std::string json_str;
		size_t pos = start + 1;
		for (; pos < html.size(); pos++) {
			if (html[pos] == '\\') {
				if (pos + 1 == html.size()) break;
				if (html[pos + 1] == 'x') {
					if (pos + 3 >= html.size()) break;
					size_t err;
					int char_code = stoi(html.substr(pos + 2, 2), &err, 16);
					if (err != 2) return error_json("failed to parse " + html.substr(pos + 2, 2) + " as hex");
					json_str.push_back(char_code);
					pos += 3;
				} else {
					json_str.push_back(html[pos + 1]);
					pos++;
				}
			} else if (html[pos] == '\'') break;
			else json_str.push_back(html[pos]);
		}
		
		std::string error;
		auto res = Json::parse(json_str, error);
		if (error != "") return error_json(error);
		return res;
	} else if (html[start] == '{') {
		size_t pos = start + 1;
		int level = 1;
		bool in_string = false;
		for (; pos < html.size(); pos++) {
			if (html[pos] == '"') in_string = !in_string;
			else if (in_string) {
				if (html[pos] == '\"') pos++;
			} else if (html[pos] == '{' || html[pos] == '[' || html[pos] == '(') level++;
			else if (html[pos] == '}' || html[pos] == ']' || html[pos] == ')') level--;
			if (level == 0) break;
		}
		if (level != 0) return error_json("the first '{' is never closed");
		
		std::string error;
		auto res = Json::parse(html.substr(start, pos - start + 1), error);
		if (error != "") return error_json(error);
		return res;
	} else {
		std::cerr << start << " " << html[start] << " " << std::to_string(html[start]) << std::endl;
		return error_json("{ or ' expected");
	}
}

static Json get_succeeding_json_regexes(const std::string &html, std::vector<const char *> patterns) {
	for (auto pattern_str : patterns) {
		std::regex pattern = std::regex(std::string(pattern_str));
		std::smatch match_res;
		if (std::regex_search(html, match_res, pattern)) {
			size_t start = match_res.suffix().first - html.begin() - 1;
			auto res = to_json(html, start);
			if (res["Error"] == Json()) return res;
			std::cerr << res["Error"].dump() << std::endl;
		}
	}
	return Json();
}
static Json get_initial_data(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialData\\s*=\\s*['\\{]"
	});
	if (res == Json()) return Json::object{{{"Error", "did not match any of the ytInitialData regexes"}}};
	return res;
}
static Json initial_player_response(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"window\\[['\\\"]ytInitialPlayerResponse['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialPlayerResponse\\s*=\\s*['\\{]"
	});
	if (res == Json()) return Json::object{{{"Error", "did not match any of the ytInitialPlayerResponse regexes"}}};
	return res;
}
static Json get_ytplayer_config(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"ytplayer\\.config\\s*=\\s*['\\{]",
		"ytInitialPlayerResponse\\s*=\\s*['\\{]"
	});
	if (res != Json()) return res;
	debug("setConfig handling");
	res = get_succeeding_json_regexes(html, {"yt\\.setConfig\\(.*['\\\"]PLAYER_CONFIG['\\\"]:\\s*['\\{]"});
	if (res != Json()) return res;
	return Json::object{{{"Error", "failed to get ytplayer config"}}};
}

bool extract_stream(YouTubeVideoInfo &res, const std::string &html) {
	Json player_response = initial_player_response(html);
	Json player_config = get_ytplayer_config(html);
	
	// extract audio stream
	std::vector<Json> formats;
	for (auto i : player_response["streamingData"]["formats"].array_items()) formats.push_back(i);
	for (auto i : player_response["streamingData"]["adaptiveFormats"].array_items()) formats.push_back(i);
	// for obfuscated signature
	std::string js_content;
	yt_cipher_transform_procedure transform_plan;
	for (auto &i : formats) { // handle decipher
		// std::cerr << i["itag"].int_value() << " " << i["mimeType"].dump() << std::endl;
		if (i["url"] != Json()) continue;
		// annoying
		if (!transform_plan.size()) { // download base js & get transform plan
			// get base js url
			std::string js_url;
			if (player_config["assets"]["js"] != Json()) js_url = player_config["assets"]["js"] != Json();
			else {
				std::regex pattern = std::regex(std::string("(/s/player/[\\w]+/[\\w-\\.]+/base\\.js)"));
				std::smatch match_res;
				if (std::regex_search(html, match_res, pattern)) js_url = match_res[1].str();
				else {
					debug("could not find js url");
					return false;
				}
			}
			js_url = "https://youtube.com" + js_url;
#   		ifdef _WIN32
				std::cerr << js_url << std::endl;
				system(("wget \"" + js_url + "\" -O test.js").c_str());
				{
					std::ifstream file("test.js");
					std::stringstream sstream;
					sstream << file.rdbuf();
					js_content = sstream.str();
				}
#   		else
#   		endif
			debug(js_content.size());
			debug(js_url);
			transform_plan = yt_get_transform_plan(js_content);
			if (!transform_plan.size()) return false; // failed to get transform plan
			for (auto i : transform_plan) debug(std::to_string(i.first) + " " + std::to_string(i.second));
		}
		auto cipher_params = parse_parameters(i["cipher"] != Json() ? i["cipher"].string_value() : i["signatureCipher"].string_value());
		auto tmp = i.object_items();
		tmp["url"] = Json(cipher_params["url"] + "&" + cipher_params["sp"] + "=" + yt_deobfuscate_signature(cipher_params["s"], transform_plan));
		i = Json(tmp);
		
	}
	std::vector<Json> audio_formats, video_formats;
	for (auto i : formats) {
		auto mime_type = i["mimeType"].string_value();
		if (mime_type.substr(0, 5) == "video") {
			// H.264 is virtually the only playable video codec
			if (mime_type.find("avc1") != std::string::npos) video_formats.push_back(i);
		} else if (mime_type.substr(0, 5) == "audio") {
			// We can modify ffmpeg to support opus
			if (mime_type.find("mp4a") != std::string::npos) audio_formats.push_back(i);
		} else {} // ???
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
	return true;
}

YouTubeVideoInfo parse_youtube_html(const std::string &html) {
	YouTubeVideoInfo res;
	Json initial_data = get_initial_data(html);
	
	Json metadata_renderer;
	{
		auto contents = initial_data["contents"]["singleColumnWatchNextResults"]["results"]["results"]["contents"];
		for (auto content : contents.array_items()) {
			if (content["itemSectionRenderer"] != Json()) {
				for (auto i : content["itemSectionRenderer"]["contents"].array_items()) if (i["slimVideoMetadataRenderer"] != Json())
					metadata_renderer = i["slimVideoMetadataRenderer"];
			}
		}
		
		std::cerr << get_text_from_object(metadata_renderer["title"]) << std::endl;
		std::cout << metadata_renderer.dump() << std::endl;
		// std::cout << get_text_from_object(metadata_renderer["description"]) << std::endl;
	}
	
	extract_stream(res, html);
	
	res.title = get_text_from_object(metadata_renderer["title"]);
	{ // set author
		res.author.name = metadata_renderer["owner"]["slimOwnerRenderer"]["channelName"].string_value();
		res.author.url = metadata_renderer["owner"]["slimOwnerRenderer"]["channelUrl"].string_value();
		// res.author.icon_url = 
		int max_height = -1;
		for (auto i : metadata_renderer["owner"]["slimOwnerRenderer"]["thumbnail"]["thumbnails"].array_items()) {
			if (max_height < i["height"].int_value()) {
				max_height = i["height"].int_value();
				res.author.icon_url = i["url"].string_value();
			}
		}
	}
	debug(res.title);
	debug(res.author.name);
	debug(res.author.url);
	debug(res.author.icon_url);
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

