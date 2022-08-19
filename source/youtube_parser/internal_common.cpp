#include <regex>
#include "internal_common.hpp"

void youtube_change_content_language(std::string language_code) {
	youtube_parser::language_code = language_code;
	youtube_parser::country_code = language_code == "en" ? "US" : "JP";
}

namespace youtube_parser {
	std::string language_code = "en";
	std::string country_code = "US";
	
#ifdef _WIN32
	std::pair<bool, std::string> http_get(const std::string &url, std::map<std::string, std::string> headers) {
		static int cnt = 0;
		static const std::string user_agent = "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36";
		if (!headers.count("User-Agent")) headers["User-Agent"] = user_agent;
		if (!headers.count("Accept-Language")) headers["Accept-Language"] = language_code + ";q=0.9";
		
		{
			std::ofstream file("wget_url.txt");
			file << url;
		}
		std::string save_file_name = "wget_tmp" + std::to_string(cnt++) + ".txt";
		
		std::string command = "wget -i wget_url.txt -O " + save_file_name + " --no-check-certificate";
		for (auto header : headers) command += " --header=\"" + header.first + ": " + header.second + "\"";
		
		system(command.c_str());
		
		std::ifstream file(save_file_name, std::ios::binary);
		std::stringstream sstream;
		sstream << file.rdbuf();
		return {true, sstream.str()};
	}
	std::pair<bool, std::string> http_post_json(const std::string &url, const std::string &json, std::map<std::string, std::string> headers) {
		{
			std::ofstream file("post_tmp.txt");
			file << json;
		}
		std::string command = "curl -X POST -H \"Content-Type: application/json\" ";
		for (auto header : headers) command += "-H \"" + header.first + ": " + header.second + "\" ";
		command += "\"" + url + "\" -o curl_tmp.txt --data-binary \"@post_tmp.txt\"";
		system(command.c_str());
		
		std::ifstream file("curl_tmp.txt", std::ios::binary);
		std::stringstream sstream;
		sstream << file.rdbuf();
		return {true, sstream.str()};
	}
#else
	static bool thread_network_session_list_inited = false;
	NetworkSessionList thread_network_session_list;
	static void confirm_thread_network_session_list_inited() {
		if (!thread_network_session_list_inited) {
			thread_network_session_list_inited = true;
			thread_network_session_list.init();
		}
	}
	
	HttpRequest http_get_request(const std::string &url, std::map<std::string, std::string> headers) {
		confirm_thread_network_session_list_inited();
		if (!headers.count("Accept-Language")) headers["Accept-Language"] = language_code + ";q=0.9";
		return HttpRequest::GET(url, headers);
	}
	std::pair<bool, std::string> http_get(const std::string &url, std::map<std::string, std::string> headers) {
		debug("accessing...");
		auto result = thread_network_session_list.perform(http_get_request(url, headers));
		if (result.fail) {
			debug("fail : " + result.error);
			return {false, result.error};
		} else {
			debug("ok");
			return {true, std::string(result.data.begin(), result.data.end())};
		}
	}
	HttpRequest http_post_json_request(const std::string &url, const std::string &json, std::map<std::string, std::string> headers) {
		confirm_thread_network_session_list_inited();
		if (!headers.count("Accept-Language")) headers["Accept-Language"] = language_code + ";q=0.9";
		headers["Content-Type"] = "application/json";
		
		return HttpRequest::POST(url, headers, json);
	}
	std::pair<bool, std::string> http_post_json(const std::string &url, const std::string &json, std::map<std::string, std::string> headers) {
		debug("accessing(POST)...");
		auto result = thread_network_session_list.perform(http_post_json_request(url, json, headers));
		if (result.fail) {
			debug("fail : " + result.error);
			return {false, result.error};
		} else {
			debug("ok");
			return {true, std::string(result.data.begin(), result.data.end())};
		}
	}
#endif
	
	bool starts_with(const std::string &str, const std::string &pattern, size_t offset) {
		return str.substr(offset, pattern.size()) == pattern;
	}
	bool ends_with(const std::string &str, const std::string &pattern) {
		return str.size() >= pattern.size() && str.substr(str.size() - pattern.size(), pattern.size()) == pattern;
	}
	
	std::string url_decode(std::string input) {
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
	std::map<std::string, std::string> parse_parameters(std::string input) {
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

	std::string get_text_from_object(RJson json) {
		if (json["simpleText"].is_valid()) return json["simpleText"].string_value();
		if (json["runs"].is_valid()) {
			std::string res;
			for (auto i : json["runs"].array_items()) res += i["text"].string_value();
			return res;
		}
		return "";
	}
	YouTubeVideoSuccinct parse_succinct_video(RJson video_renderer) {
		YouTubeVideoSuccinct res;
		std::string video_id = video_renderer["videoId"].string_value();
		if (video_id == "") debug("!!!!!!!!!!!!!!!!!!!!! video id empty !!!!!!!!!!!!!!!!!!!!!!!!!");
		res.url = "https://m.youtube.com/watch?v=" + video_id;
		res.title = get_text_from_object(video_renderer["title"]);
		if (res.title == "") res.title = get_text_from_object(video_renderer["headline"]);
		res.duration_text = get_text_from_object(video_renderer["lengthText"]);
		res.publish_date = get_text_from_object(video_renderer["publishedTimeText"]);
		res.views_str = get_text_from_object(video_renderer["shortViewCountText"]);
		// debug(get_text_from_object(video_renderer["shortViewCountText"]) + " vs " + get_text_from_object(video_renderer["viewCountText"]));
		res.author = get_text_from_object(video_renderer["shortBylineText"]);
		res.thumbnail_url = youtube_get_video_thumbnail_url_by_id(video_id);
		
		return res;
	}
	std::string get_thumbnail_url_closest(RJson thumbnails, int target_width) {
		int min_distance = 100000;
		std::string best_icon_url;
		for (auto thumbnail : thumbnails.array_items()) {
			int cur_width = thumbnail["width"].int_value();
			if (min_distance > std::abs(target_width - cur_width)) {
				min_distance = std::abs(target_width - cur_width);
				best_icon_url = thumbnail["url"].string_value();
			}
		}
		if (best_icon_url.substr(0, 2) == "//") best_icon_url = "https:" + best_icon_url;
		return best_icon_url;
	}
	std::string get_thumbnail_url_exact(RJson thumbnails, int target_width) {
		if (!thumbnails.array_items().size()) return "";
		auto thumbnail = thumbnails.array_items()[0];
		std::string icon_url = thumbnail["url"].string_value();
		std::string icon_url_modified;
		std::string replace_from = std::to_string(thumbnail["width"].int_value()) + "-";
		std::string replace_to = std::to_string(target_width) + "-";
		for (size_t i = 0; i < icon_url.size(); ) {
			if ((icon_url[i] == 's' || icon_url[i] == 'w') && icon_url.substr(i + 1, replace_from.size()) == replace_from) {
				icon_url_modified.push_back(icon_url[i]);
				icon_url_modified += replace_to;
				i += 1 + replace_from.size();
			} else icon_url_modified.push_back(icon_url[i++]);
		}
		if (icon_url_modified.substr(0, 2) == "//") icon_url_modified = "https:" + icon_url_modified;
		return icon_url_modified;
	}

	std::string remove_garbage(const std::string &str, size_t start) {
		while (start < str.size() && str[start] == ' ') start++;
		if (start >= str.size()) {
			debug("remove_garbage : empty");
			return "";
		}
		if (str[start] == '\'') {
			std::string res_str;
			size_t pos = start + 1;
			for (; pos < str.size(); pos++) {
				if (str[pos] == '\\') {
					if (pos + 1 == str.size()) break;
					if (str[pos + 1] == 'x') {
						if (pos + 3 >= str.size()) break;
						int char_code = 0;
						bool ok = true;
						for (int i = 0; i < 2; i++) {
							if (pos + 2 + i >= str.size()) {
								ok = false;
								break;
							}
							char cur_char = str[pos + 2 + i];
							char_code <<= 4; // * 16
							if ('0' <= cur_char && cur_char <= '9') char_code += cur_char - '0';
							else if ('a' <= cur_char && cur_char <= 'f') char_code += cur_char - 'a' + 10;
							else if ('A' <= cur_char && cur_char <= 'F') char_code += cur_char - 'A' + 10;
							else {
								ok = false;
								break;
							}
						}
						if (!ok) {
							debug("remove_garbage : failed to parse " + str.substr(pos + 2, 2) + " as hex");
							return "";
						}
						res_str.push_back(char_code);
						pos += 3;
					} else {
						res_str.push_back(str[pos + 1]);
						pos++;
					}
				} else if (str[pos] == '\'') break;
				else res_str.push_back(str[pos]);
			}
			return res_str;
		} else if (str[start] == '(' || str[start] == '{' || str[start] == '[') {
			size_t pos = start + 1;
			int level = 1;
			bool in_string = false;
			for (; pos < str.size(); pos++) {
				if (str[pos] == '"') in_string = !in_string;
				else if (in_string) {
					if (str[pos] == '\\') pos++;
				} else if (str[pos] == '{' || str[pos] == '[' || str[pos] == '(') level++;
				else if (str[pos] == '}' || str[pos] == ']' || str[pos] == ')') level--;
				if (level == 0) break;
			}
			if (level != 0) {
				debug("remove_garbage : the first parenthesis is never closed");
				return "";
			}
			return str.substr(start, pos - start + 1);
		} else {
			debug("remove_garbage : (, {, [, or ' expected");
			return "";
		}
	}
	// `html` can contain unnecessary garbage at the end of the actual json data
	RJson to_json(Document &json_root, const std::string &html, size_t start) {
		auto content = remove_garbage(html, start);
		std::string error;
		auto res = RJson::parse(json_root, (char *) &content[0], error);
		if (error != "") get_error_json(error);
		return res;
	}
	// search for `var_name` = ' or `var_name` = {
	bool fast_extract_initial(Document &json_root, const std::string &html, const std::string &var_name, RJson &res) {
		size_t head = 0;
		while (head < html.size()) {
			auto pos = html.find(var_name, head);
			if (pos == std::string::npos) break;
			pos += var_name.size();
			while (pos < html.size() && isspace(html[pos])) pos++;
			if (pos < html.size() && html[pos] == '=') {
				pos++;
				while (pos < html.size() && isspace(html[pos])) pos++;
				if (pos < html.size() && (html[pos] == '\'' || html[pos] == '{')) {
					res = to_json(json_root, html, pos);
					if (res.has_key("Error")) debug(std::string("fast_extract_initial : ") + res["Error"].string_value());
					else if (res.is_valid()) return true;
				}
			}
			head = pos;
		}
		return false;
	}
	RJson get_succeeding_json_regexes(Document &json_root, const std::string &html, std::vector<const char *> patterns) {
		for (auto pattern_str : patterns) {
			std::regex pattern = std::regex(std::string(pattern_str));
			std::smatch match_res;
			if (std::regex_search(html, match_res, pattern)) {
				size_t start = match_res.suffix().first - html.begin() - 1;
				auto res = to_json(json_root, html, start);
				if (!res.has_key("Error")) return res;
			}
		}
		return RJson();
	}

	std::string convert_url_to_mobile(std::string url) {
		// strip out of http:// or https://
		{
			auto pos = url.find("://");
			if (pos != std::string::npos) url = url.substr(pos + 3, url.size());
		}
		if (url.substr(0, 4) == "www.") url = "m." + url.substr(4, url.size());
		return "https://" + url;
	}
	std::string convert_url_to_desktop(std::string url) {
		// strip out of http:// or https://
		{
			auto pos = url.find("://");
			if (pos != std::string::npos) url = url.substr(pos + 3, url.size());
		}
		if (url.substr(0, 2) == "m.") url = "www." + url.substr(2, url.size());
		return "https://" + url;
	}
	
	RJson get_error_json(const std::string &error) {
		Document data;
		data.SetObject();
		Value key;
		key = "Error";
		Value value;
		value.SetString(error.c_str(), data.GetAllocator());
		data.AddMember(key, value, data.GetAllocator());
		return RJson(data);
	}
}
