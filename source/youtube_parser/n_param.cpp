#include <vector>
#include <utility>
#include <regex>
#include <string>
#include <map>
#include "duktape/duktape.h"
#include "internal_common.hpp"
#include "n_param.hpp"

#ifdef _WIN32
#include <iostream> // <------------
#include <fstream> // <-------
#include <sstream> // <-------
#define debug(s) std::cerr << (s) << std::endl
#else
#include "headers.hpp"
#endif

// array(array of nparam decrypt function(s)) name, index
static std::string get_initial_function_name(const std::string &js) {
	const std::string prefix = ".get(";
	const std::string middle = "\"n\"";
	const std::string suffix = "))&&(?=";
	
	size_t head = 0;
	std::vector<std::pair<std::string, int> > candidates;
	while (head < js.size()) {
		auto pos = js.find(middle, head);
		if (pos == std::string::npos) break;
		pos += middle.size();
		head = pos;
		
		if (pos >= prefix.size() + middle.size() && js.substr(pos - prefix.size() - middle.size(), prefix.size() + middle.size()) == prefix + middle) {
			bool ok = true;
			for (size_t i = 0; i < suffix.size(); i++) if (suffix[i] != '?' && suffix[i] != js[pos + i]) {
				ok = false;
				break;
			}
			if (!ok) continue;
			std::string cur_name;
			pos += suffix.size();
			while (pos < js.size() && isalnum(js[pos])) cur_name.push_back(js[pos++]);
			int array_index = -1;
			if (pos < js.size() && js[pos] == '[') {
				pos++;
				array_index = 0;
				while (pos < js.size() && isdigit(js[pos])) array_index = array_index * 10 + js[pos++] - '0';
			}
			candidates.push_back({cur_name, array_index});
		}
	}
	if (candidates.size() != 1) {
		debug("[nparam] initial funciton name candidate num : " + std::to_string(candidates.size()));
		return "";
	}
	std::string var_name = candidates[0].first;
	int array_index = candidates[0].second;
	if (array_index == -1) { // until 2022-02-01, the variable used was the nparam restoration function itself
		return var_name;
	} else { // now, it's an array containing the restoration function
		std::string pattern = "var " + var_name + "=[";
		auto pos = js.find(pattern);
		if (pos == std::string::npos) {
			debug("[nparam] the array containing initial function not found : " + var_name);
			return "";
		}
		pos += pattern.size();
		// assuming there are only variable(function) names in the array
		for (int i = 0; i <= array_index; i++) {
			auto start = pos;
			while (pos < js.size() && js[pos] != ',' && js[pos] != ']') pos++;
			if (i == array_index)
				return js.substr(start, pos - start);
			pos++;
		}
		return ""; // never reached, just to suppress the warning
	}
}


std::string yt_nparam_get_function_content(const std::string &js) {
	std::string res;
	{
		std::string name = get_initial_function_name(js);
		if (name == "") {
			debug("Failed to get nparam transform function");
			return {};
		}
		auto pos = js.find(name + "=function(");
		if (pos != std::string::npos) {
			auto start_pos = pos + (name + "=").size();
			pos += (name + "=function(").size();
			pos = std::find(js.begin() + pos, js.end(), ')') - js.begin();
			res = js.substr(start_pos, pos + 1 - start_pos);
			if (pos + 1 < js.size()) res += remove_garbage(js, pos + 1);
			else debug("[nparam] unexpected : function definition truncated");
		} else {
			debug("nparam transform function definition not found");
			return "";
		}
	}
	
	return res;
}
std::string yt_modify_nparam(std::string n_param, const std::string &function) {
	duk_context *js_context = duk_create_heap_default();
	
	std::string script = "(" + function + ")(\"" + n_param + "\");";
	duk_push_string(js_context, script.c_str());
	if (duk_peval(js_context) != 0) duk_safe_to_stacktrace(js_context, -1);
	else duk_safe_to_string(js_context, -1);
	
	const char *res_tmp = duk_get_string(js_context, -1);
	std::string res = res_tmp ? res_tmp : "";
	duk_pop(js_context);
	duk_destroy_heap(js_context);
	
	return res;
}

