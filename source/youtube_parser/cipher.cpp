#include <vector>
#include <utility>
#include <regex>
#include <string>
#include <map>
#include "cipher.hpp"
#include "internal_common.hpp"

#ifdef _WIN32
#include <iostream> // <------------
#include <fstream> // <-------
#include <sstream> // <-------
#define debug(s) std::cerr << (s) << std::endl
#else
#include "headers.hpp"
#endif


// a bit costly, so it's called only when the simpler detection failed
// refer to https://github.com/pytube/pytube/blob/48ea5205b92e8e090866589be67780ce8e29a922/pytube/cipher.py#L104
static std::string get_initial_function_name_precise(const std::string &js) {
	// confusing but they are raw string literals : R"(ACTUAL_STRING)"
	std::vector<std::string> function_patterns = {
        R"(\b[cs]\s*&&\s*[adf]\.set\([^,]+\s*,\s*encodeURIComponent\s*\(\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"(\b[a-zA-Z0-9]+\s*&&\s*[a-zA-Z0-9]+\.set\([^,]+\s*,\s*encodeURIComponent\s*\(\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"((?:\b|[^a-zA-Z0-9$])([a-zA-Z0-9$]{2})\s*=\s*function\(\s*a\s*\)\s*\{\s*a\s*=\s*a\.split\(\s*""\s*\))",  // noqa: E501
        R"(([a-zA-Z0-9$]+)\s*=\s*function\(\s*a\s*\)\s*\{\s*a\s*=\s*a\.split\(\s*""\s*\))",  // noqa: E501
        R"((["\'])signature\1\s*,\s*([a-zA-Z0-9$]+)\()",
        R"(\.sig\|\|([a-zA-Z0-9$]+)\()",
        R"(yt\.akamaized\.net/\)\s*\|\|\s*.*?\s*[cs]\s*&&\s*[adf]\.set\([^,]+\s*,\s*(?:encodeURIComponent\s*\()?\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"(\b[cs]\s*&&\s*[adf]\.set\([^,]+\s*,\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"(\b[a-zA-Z0-9]+\s*&&\s*[a-zA-Z0-9]+\.set\([^,]+\s*,\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"(\bc\s*&&\s*a\.set\([^,]+\s*,\s*\([^)]*\)\s*\(\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"(\bc\s*&&\s*[a-zA-Z0-9]+\.set\([^,]+\s*,\s*\([^)]*\)\s*\(\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
        R"(\bc\s*&&\s*[a-zA-Z0-9]+\.set\([^,]+\s*,\s*\([^)]*\)\s*\(\s*([a-zA-Z0-9$]+)\()",  // noqa: E501
	};
	for (auto pattern_str : function_patterns) {
		std::regex pattern(pattern_str);
		std::smatch match_res;
		if (std::regex_search(js, match_res, pattern))
			return match_res[1].str();
	}
	
	return "";
}

/*
	return : vector of operations
	each operation :
		first : type of operation
			0. reverse() : std::reverse(a.begin(), a.end());
			1. splice(b) : a = a.substr(b, a.size() - b)
			2. swap(b) : std::swap(a[0], a[b % a.size()])
		second : the argument 'b' (if first == 0, this should be ignored)
*/
yt_cipher_transform_procedure yt_cipher_get_transform_plan(const std::string &js) {
	// get initial function name
	std::string initial_func_content;
	{
		bool fast_detect_ok = false;
		{
			// search for   =function(?){?=?.split("")   where ? refers to the same one character (seems like it's always 'a' though)
			size_t head = 0;
			std::vector<std::string> candidates;
			
			const std::string prefix = "function(?){?=?.spl";
			const std::string middle = "it(\"\");";
			while (head < js.size()) {
				auto pos = js.find(middle, head);
				if (pos == std::string::npos) break;
				
				if (pos >= std::string(prefix).size()) {
					pos -= std::string(prefix).size();
					char var_name;
					bool var_name_first = true;
					bool ok = true;
					for (size_t i = 0; i < prefix.size(); i++) {
						if (prefix[i] == '?') {
							if (var_name_first) var_name = js[pos + i];
							else if (var_name != js[pos + i]) {
								ok = false;
								break;
							}
						} else if (prefix[i] != js[pos + i]) {
							ok = false;
							break;
						}
					}
					if (ok) {
						size_t start = pos + prefix.size() + middle.size();
						size_t end = start;
						int level = 1;
						for (; end < js.size() && level; end++) {
							if (js[end] == '}') level--;
							else if (js[end] == '{') level++;
						}
						if (start < end) {
							while (--end >= start && js[end] != ';') end--;
							candidates.push_back(js.substr(start, end - start));
						}
					}
					pos += prefix.size();
				}
				head = pos + middle.size();
			}
			if (candidates.size() == 1) {
				initial_func_content = candidates[0];
				fast_detect_ok = true;
			}
		}
		
		if (!fast_detect_ok) {
			debug("simple detection failed, conducting full detection");
			std::string initial_func_name = get_initial_function_name_precise(js);
			debug("initial func name : " + initial_func_name);
			std::regex content_regex(initial_func_name + R"(=function\(\w\)\{[a-z=\.\(\"\)]*;(.*);(?:.+)\})");
			std::smatch match_res;
			if (regex_search(js, match_res, content_regex)) {
				initial_func_content = match_res[1].str();
			} else {
				debug("unexpected error in getting initial func content");
				return {};
			}
		}
	}
	std::string var_name;
	for (auto c : initial_func_content) {
		if (!isdigit(c) && !isalpha(c) && c != '_' && c != '$') break;
		var_name.push_back(c);
	}
	
	// search for the definition of var_name, create the map of its member function names and operation types
	std::map<std::string, int> operation_map;
	{
		// search for the variable definition
		size_t head = 0;
		std::vector<size_t> candidates;
		// search for  var [var_name] = {...}
		while (head < js.size()) {
			auto pos = js.find(var_name, head);
			if (pos == std::string::npos) break;
			size_t left = pos;
			while (left && isspace(js[left - 1])) left--;
			size_t right = pos + var_name.size();
			while (right < js.size() && isspace(js[right])) right++;
			if (left >= 3 && js.substr(left - 3, 3) == "var" && right < js.size() && js[right] == '=') {
				while (++right < js.size() && isspace(js[right]));
				if (js[right] == '{') candidates.push_back(right);
			}
			
			head = pos + var_name.size();
		}
		if (candidates.size() != 1) {
			debug("[cipher] variable definition number not 1 : " + std::to_string(candidates.size()));
			return {};
		}
		std::string definition = remove_garbage(js, candidates[0]);
		if (definition.size() < 2) {
			debug("[cipher] unexpected : definition size " + std::to_string(definition.size()));
			return {};
		}
		definition.erase(definition.begin());
		definition.pop_back();
		
		std::string cur_definition;
		for (size_t i = 0; i <= definition.size(); i++) {
			if (i == definition.size() || (i && definition[i] == ',' && definition[i - 1] == '}')) {
				auto colon = std::find(cur_definition.begin(), cur_definition.end(), ':');
				if (colon == cur_definition.end()) {
					debug("colon expected in definition");
					return {};
				}
				std::string func_name(cur_definition.begin(), colon);
				std::string func_content(colon + 1, cur_definition.end());
				if (func_content.find("reverse") != std::string::npos) operation_map[func_name] = 0;
				else if (func_content.find("splice") != std::string::npos) operation_map[func_name] = 1;
				else operation_map[func_name] = 2;
				
				cur_definition = "";
			} else if (!isspace(definition[i])) cur_definition.push_back(definition[i]);
		}
	}
	
	yt_cipher_transform_procedure res;
	std::vector<std::string> operation_strs = {""};
	for (auto c : initial_func_content) {
		if (c == ';') operation_strs.push_back("");
		else operation_strs.back().push_back(c);
	}
	for (auto operation_str : operation_strs) {
		if (!operation_str.size()) continue;
		
		size_t head = 0;
		while (head < operation_str.size() && (isalnum(operation_str[head]) || operation_str[head] == '_')) head++;
		
		std::string op_name;
		int arg = -1;
		if (head < operation_str.size() && operation_str[head] == '.') {
			auto par0 = std::find(operation_str.begin() + head + 1, operation_str.end(), '(');
			auto par1 = std::find(par0, operation_str.end(), ',');
			auto par2 = std::find(par1, operation_str.end(), ')');
			if (par2 == operation_str.end()) {
				debug("[cipher] bad line : " + operation_str);
				continue;
			} else {
				op_name = operation_str.substr(head + 1, par0 - operation_str.begin() - (head + 1));
				arg = stoi(std::string(par1 + 1, par2));
			}
		} else if (head + 1 < operation_str.size() && operation_str.substr(head, 2) == "[\"") {
			auto par1 = std::find(operation_str.begin() + head + 2, operation_str.end(), ']');
			if (operation_str.substr(par1 - operation_str.begin() - 1, 3) == "\"](") {
				op_name = std::string(operation_str.begin() + head + 2, par1 - 1);
				auto par2 = std::find(par1 + 2, operation_str.end(), ',');
				arg = stoi(std::string(par1 + 2, par2));
			} else {
				debug("[cipher] bad line : " + operation_str);
				continue;
			}
		} else {
			debug("[cipher] unknown operation line : " + operation_str);
			continue;
		}
		if (!operation_map.count(op_name)) {
			debug("op name not defined : " + op_name);
			return {};
		}
		res.push_back({operation_map[op_name], arg});
	}
	
	return res;
}


std::string yt_deobfuscate_signature(std::string sig, const yt_cipher_transform_procedure &transform_plan) {
	for (auto i : transform_plan) {
		if (i.first == 0) std::reverse(sig.begin(), sig.end());
		else if (i.first == 1) {
			if (i.second < 0 || i.second > (int) sig.size()) {
				debug("[cipher] OoB in splice()");
				return "";
			}
			sig = sig.substr(i.second, sig.size() - i.second);
		} else if (i.first == 2) {
			if (i.second < 0) {
				debug("[cipher] OoB in swap()");
				return "";
			}
			std::swap(sig[0], sig[i.second % sig.size()]);
		} else {
			debug("[cipher] unknown operation type : " + std::to_string(i.first));
			return "";
		}
	}
	
	return sig;
}
