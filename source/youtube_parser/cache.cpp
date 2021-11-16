#include <sstream>
#include <string>
#include "cache.hpp"
#include "internal_common.hpp"


#define CACHE_VERSION 2

std::string yt_procs_to_string(const yt_cipher_transform_procedure &cipher_proc, const std::string &nparam_func) {
	std::ostringstream stream;
	
	stream << "version " << CACHE_VERSION << std::endl;
	
	for (auto proc : cipher_proc) stream << "cipher_proc " << proc.first << " " << proc.second << std::endl;
	
	std::string nparam_function_oneline;
	for (auto c : nparam_func) {
		if (c == '\n') nparam_function_oneline.push_back(' ');
		else if (c != '\r') nparam_function_oneline.push_back(c);
	}
	stream << "nparam_func " << nparam_function_oneline << std::endl;
	stream << "end" << std::endl;
	
	return stream.str();
}
bool yt_procs_from_string(const std::string &str, yt_cipher_transform_procedure &cipher_proc, std::string &nparam_func) {
	cipher_proc = yt_cipher_transform_procedure();
	nparam_func = "";
	
	std::istringstream stream(str);
	int version = -1;
	std::string command;
	if (!(stream >> command)) {
		debug("[cache] Empty cache");
		return false;
	}
	if (command != "version" || !(stream >> version)) {
		debug("[cache] Cache version expected");
		return false;
	}
	if (version < 0 || version > CACHE_VERSION) {
		debug("[cache] " + std::string(version < 0 ? "Invalid cache version" : "Unsupported version"));
		return false;
	}
	if (version < CACHE_VERSION) {
		debug("[cache] cache made by an outdated version of the app, ignoring...");
		return false;
	}
	
	int command_cnt = 1;
	while (stream >> command) {
		std::string log_prefix = "[cache] " + std::to_string(command_cnt) + " ";
		
		if (command == "cipher_proc") {
			int first, second;
			if (!(stream >> first >> second)) {
				debug(log_prefix + "cipher_proc : two integers expected");
				return false;
			}
			cipher_proc.push_back({first, second});
		} else if (command == "nparam_func") {
			std::getline(stream, nparam_func);
		} else if (command == "end") return true;
		command_cnt++;
	}
	
	debug("[cache] No end");
	return false;
}

