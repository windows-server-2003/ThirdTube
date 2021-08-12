#include <regex>
#include "internal_common.hpp"

#ifndef _WIN32
#include "network/network_io.hpp"
#endif

void youtube_change_content_language(std::string language_code) {
	youtube_parser::language_code = language_code;
	youtube_parser::country_code = language_code == "en" ? "US" : "JP";
}

namespace youtube_parser {
	std::string language_code = "en";
	std::string country_code = "US";
	
#ifdef _WIN32
	std::string http_get(const std::string &url, std::map<std::string, std::string> header) {
		static int cnt = 0;
		static const std::string user_agent = "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36";
		if (!header.count("User-Agent")) header["User-Agent"] = user_agent;
		if (!header.count("Accept-Language")) header["Accept-Language"] = language_code + ";q=0.9";
		
		{
			std::ofstream file("wget_url.txt");
			file << url;
		}
		std::string save_file_name = "wget_tmp" + std::to_string(cnt++) + ".txt";
		
		std::string command = "wget -i wget_url.txt -O " + save_file_name + " --no-check-certificate";
		for (auto i : header) command += " --header=\"" + i.first + ": " + i.second + "\"";
		
		system(command.c_str());
		std::ifstream file(save_file_name);
		std::stringstream sstream;
		sstream << file.rdbuf();
		return sstream.str();
	}
	std::string http_post_json(const std::string &url, const std::string &json) {
		{
			std::ofstream file("post_tmp.txt");
			file << json;
		}
		system(("curl -X POST -H \"Content-Type: application/json\" " + url + " -o curl_tmp.txt --data-binary \"@post_tmp.txt\"").c_str());
		
		std::ifstream file("curl_tmp.txt");
		std::stringstream sstream;
		sstream << file.rdbuf();
		return sstream.str();
	}
#else
	static bool thread_network_session_list_inited = false;
	static NetworkSessionList thread_network_session_list;
	static void confirm_thread_network_session_list_inited() {
		if (!thread_network_session_list_inited) {
			thread_network_session_list_inited = true;
			thread_network_session_list.init();
		}
	}
	
	std::string http_get(const std::string &url, std::map<std::string, std::string> header) {
		confirm_thread_network_session_list_inited();
		if (!header.count("Accept-Language")) header["Accept-Language"] = language_code + ";q=0.9";
		
		add_cpu_limit(25);
		debug("accessing...");
		auto result = Access_http_get(thread_network_session_list, url, header);
		if (result.fail) debug("fail : " + result.error);
		else debug("ok");
		remove_cpu_limit(25);
		result.finalize();
		return std::string(result.data.begin(), result.data.end());
	}
	std::string http_post_json(const std::string &url, const std::string &json) {
		confirm_thread_network_session_list_inited();
		add_cpu_limit(25);
		debug("accessing(POST)...");
		auto result = Access_http_post(thread_network_session_list, url, {{"Content-Type", "application/json"}}, json);
		if (result.fail) debug("fail : " + result.error);
		else debug("ok");
		remove_cpu_limit(25);
		result.finalize();
		return std::string(result.data.begin(), result.data.end());
	}
#endif
	
	bool starts_with(const std::string &str, const std::string &pattern, size_t offset) {
		return str.substr(offset, pattern.size()) == pattern;
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

	std::string get_text_from_object(Json json) {
		if (json["simpleText"] != Json()) return json["simpleText"].string_value();
		if (json["runs"] != Json()) {
			std::string res;
			for (auto i : json["runs"].array_items()) res += i["text"].string_value();
			return res;
		}
		return "";
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
	Json to_json(const std::string &html, size_t start) {
		auto error_json = [&] (std::string error) {
			return Json::object{{{"Error", error}}};
		};
		auto content = remove_garbage(html, start);
		std::string error;
		auto res = Json::parse(content, error);
		if (error != "") return error_json(error);
		return res;
	}
	// search for `var_name` = ' or `var_name` = {
	bool fast_extract_initial(const std::string &html, const std::string &var_name, Json &res) {
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
					res = to_json(html, pos);
					if (res != Json()) return true;
				}
			}
			head = pos;
		}
		return false;
	}
	Json get_succeeding_json_regexes(const std::string &html, std::vector<const char *> patterns) {
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
}
