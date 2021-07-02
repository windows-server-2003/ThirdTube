#include <vector>
#include <utility>
#include <regex>
#include <string>
#include <map>
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
		// std::cerr << pattern_str << std::endl;
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
yt_cipher_transform_procedure yt_get_transform_plan(const std::string &js) {
	// get initial function name
	std::string initial_func_content;
	{
		std::regex simple_regex(std::string(R"((\w+)=function\(\w\)\{\w+=\w+\.split\(\"\"\);([^\{\}]*);(?:[^\{\}]+)\})"));
		std::smatch match_res;
		bool simple_detect_ok = true;
		if (regex_search(js, match_res, simple_regex)) {
			initial_func_content = match_res[2].str();
			if (regex_search(match_res.suffix().first, js.end(), match_res, simple_regex)) simple_detect_ok = false;
		} else {
			debug("simple regex doesn't match at all");
			simple_detect_ok = false;
		}
		if (!simple_detect_ok) {
			debug("simple detection failed, conducting full detection");
			std::string initial_func_name = get_initial_function_name_precise(js);
			debug("initial func name : " + initial_func_name);
			std::regex content_regex(initial_func_name + R"(=function\(\w\)\{[a-z=\.\(\"\)]*;(.*);(?:.+)\})");
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
	debug("var name : " + var_name);
	
	// search for the definition of var_name, create the map of its member function names and operation types
	std::map<std::string, int> operation_map;
	{
		std::regex pattern(std::string("var " + var_name + R"(\s*=\s*\{([\s\S]*?)\};)"));
		std::smatch match_res;
		std::string definition;
		if (regex_search(js, match_res, pattern)) {
			for (auto c : match_res[1].str()) if (c != ' ' && c != '\r' && c != '\n') definition.push_back(c);
		} else {
			debug("definition of the transformer var (" + var_name + ") not found");
			return {};
		}
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
			} else cur_definition.push_back(definition[i]);
		}
		
	}
	
	yt_cipher_transform_procedure res;
	std::string cur_operation;
	for (size_t i = 0; i <= initial_func_content.size(); i++) {
		if (i == initial_func_content.size() || initial_func_content[i] == ';') {
			std::vector<std::string> patterns = {
				R"(\w+\.(\w+)\(\w,(\d+)\))",
				R"(\w+\[(\"\w+\")\]\(\w,(\d+)\))",
			};
			for (auto pattern_str : patterns) {
				std::regex pattern(pattern_str);
				std::smatch match_res;
				if (std::regex_search(cur_operation, match_res, pattern)) {
					std::string op_name = match_res[1].str();
					if (!operation_map.count(op_name)) {
						debug("op name not defined : " + op_name);
						return {};
					}
					int op = operation_map[op_name];
					int arg = stoi(match_res[2].str());
					res.push_back({op, arg});
					break;
				}
			}
			cur_operation = "";
		} else cur_operation.push_back(initial_func_content[i]);
	}
	
	return res;
}


std::string yt_deobfuscate_signature(std::string sig, const yt_cipher_transform_procedure &transform_plan) {
	for (auto i : transform_plan) {
		if (i.first == 0) std::reverse(sig.begin(), sig.end());
		else if (i.first == 1) sig = sig.substr(i.second, sig.size() - i.second);
		else std::swap(sig[0], sig[i.second % sig.size()]);
	}
	
	return sig;
}
