/*
	reference :
	https://github.com/pytube/pytube
*/

#include <string>
#include <regex>
#include "parser.hpp"
#include "json11/json11.hpp"

#ifdef _WIN32
#	include <iostream> // <------------
#	include <fstream> // <-------
#	include <sstream> // <-------

	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	typedef int8_t s8;
	typedef int16_t s16;
	typedef int32_t s32;
	typedef int64_t s64;

#	define debug(s) std::cerr << (s) << std::endl

	static std::string http_get(const std::string &url) {
		static const std::string user_agent = "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36";
		
		system(("wget --user-agent=\"" + user_agent + "\" \"" + url + "\" -O wget_tmp.txt").c_str());
		std::ifstream file("wget_tmp.txt");
		std::stringstream sstream;
		sstream << file.rdbuf();
		return sstream.str();
	}

#else // if it's a 3ds...

#	include "headers.hpp"
#	define debug(s) Util_log_save("yt-parser", (s));

	static std::string http_get(const std::string &url) {
		constexpr int BLOCK = 0x40000; // 256 KB
		add_cpu_limit(25);
		debug("accessing...");
		// use mobile version of User-Agent for smaller webpage (and the whole parser is designed to parse the mobile version)
		auto network_res = access_http(url, {{"User-Agent", "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36"}});
		std::string res;
		if (network_res.first == "") {
			debug("downloading...");
			std::vector<u8> buffer(BLOCK);
			std::vector<u8> res_vec;
			while (1) {
				u32 len_read;
				Result ret = httpcDownloadData(&network_res.second, &buffer[0], BLOCK, &len_read);
				res_vec.insert(res_vec.end(), buffer.begin(), buffer.begin() + len_read);
				if (ret != (s32) HTTPC_RESULTCODE_DOWNLOADPENDING) break;
			}
			
			httpcCloseContext(&network_res.second);
			res = std::string(res_vec.begin(), res_vec.end());
		} else Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "failed accessing : " + network_res.first);
		
		remove_cpu_limit(25);
		return res;
	}

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


static std::string convert_url_to_mobile(std::string url) {
	// strip out of http:// or https://
	{
		auto pos = url.find("://");
		if (pos != std::string::npos) url = url.substr(pos + 3, url.size());
	}
	if (url.substr(0, 4) == "www.") url = "m." + url.substr(4, url.size());
	return "https://" + url;
}

YouTubeSearchResult parse_search(std::string url) {
	YouTubeSearchResult res;
	
	url = convert_url_to_mobile(url);
	
	std::string html = http_get(url);
	if (!html.size()) {
		res.error = "failed to download video page";
		return res;
	}
	
	auto initial_data = get_initial_data(html);
	res.estimated_result_num = std::stoll(initial_data["estimatedResults"].string_value());
	
	for (auto i : initial_data["contents"]["sectionListRenderer"]["contents"].array_items()) {
		if (i["itemSectionRenderer"] != Json()) {
			for (auto j : i["itemSectionRenderer"]["contents"].array_items()) {
				auto video_renderer = j["compactVideoRenderer"];
				YouTubeSearchResult::VideoInfo cur_result;
				cur_result.url = "https://m.youtube.com/watch?v=" + video_renderer["navigationEndpoint"]["watchEndpoint"]["videoId"].string_value();
				cur_result.title = get_text_from_object(video_renderer["title"]);
				cur_result.duration_text = get_text_from_object(video_renderer["lengthText"]);
				cur_result.author = get_text_from_object(video_renderer["shortBylineText"]);
				{ // extract thumbnail url
					int max_width = -1;
					for (auto thumbnail : video_renderer["thumbnail"]["thumbnails"].array_items()) {
						int cur_width = thumbnail["width"].int_value();
						if (max_width < cur_width) {
							max_width = cur_width;
							cur_result.thumbnail_url = thumbnail["url"].string_value();
						}
					}
				}
				res.results.push_back(cur_result);
			}
		}
		if (i["continuationItemRenderer"] != Json()) {
			std::string continuation_string = i["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
			debug(continuation_string);
		}
	}
	{
		std::regex pattern(std::string(R"***("INNERTUBE_API_KEY":"([\w-]+)")***"));
		std::smatch match_result;
		if (std::regex_search(html, match_result, pattern)) {
			debug(match_result[1].str());
		} else {
			debug("INNERTUBE_API_KEY not found");
			res.error = "INNERTUBE_API_KEY not found";
			return res;
		}
	}
	
	/*
	for (auto i : res.results) {
		debug(i.title + " " + i.author + " " + i.duration_text + " " + i.url + " " + i.thumbnail_url);
	}
	debug(res.estimated_result_num);*/
	return res;
}
/*
#ifdef _WIN32
int main() {
	std::string url;
	std::cin >> url;
	parse_search(url);
	return 0;
}
#endif*/

