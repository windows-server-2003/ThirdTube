#include <sstream>
#include <string>
#include "cache.hpp"
#include "internal_common.hpp"


#define CACHE_VERSION 0

std::string yt_procs_to_string(const yt_cipher_transform_procedure &cipher_proc, const yt_nparam_transform_procedure &nparam_proc) {
	std::ostringstream stream;
	
	stream << "version " << CACHE_VERSION << std::endl;
	
	for (auto proc : cipher_proc) stream << "cipher_proc " << proc.first << " " << proc.second << std::endl;
	
	for (auto element : nparam_proc.c) {
		stream << "nparam_c ";
		if (element.type == NParamCArrayContent::Type::INTEGER ) stream << "int " << element.integer;
		if (element.type == NParamCArrayContent::Type::STRING  ) stream << "str " << std::string(element.string.begin(), element.string.end());
		if (element.type == NParamCArrayContent::Type::N       ) stream << "n   ";
		if (element.type == NParamCArrayContent::Type::SELF    ) stream << "self";
		if (element.type == NParamCArrayContent::Type::FUNCTION) {
			stream << "function " << (int) element.function;
			if (element.function == NParamFunctionType::CIPHER) stream << " " << element.function_internal_arg;
		}
		stream << std::endl;
	}
	
	for (auto op : nparam_proc.ops) stream << "nparam_op " << op.first << " " << op.second.first << " " << op.second.second << std::endl;
	
	stream << "end" << std::endl;
	
	return stream.str();
}
bool yt_procs_from_string(const std::string &str, yt_cipher_transform_procedure &cipher_proc, yt_nparam_transform_procedure &nparam_proc) {
	cipher_proc = yt_cipher_transform_procedure();
	nparam_proc = yt_nparam_transform_procedure();
	
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
		} else if (command == "nparam_c") {
			std::string type;
			if (!(stream >> type)) {
				debug(log_prefix + "nparam_c : type expected");
				return false;
			}
			NParamCArrayContent element;
			bool read_fail = false;
			bool invalid_value = false;
			if (type == "int") {
				element.type = NParamCArrayContent::Type::INTEGER;
				if (!(stream >> element.integer)) read_fail = true;
			} else if (type == "str") {
				element.type = NParamCArrayContent::Type::STRING;
				std::string tmp;
				if (!(stream >> tmp)) read_fail = true;
				else element.string = std::vector<char>(tmp.begin(), tmp.end());
			} else if (type == "n") {
				element.type = NParamCArrayContent::Type::N;
			} else if (type == "self") {
				element.type = NParamCArrayContent::Type::SELF;
			} else if (type == "function") {
				element.type = NParamCArrayContent::Type::FUNCTION;
				int function_type;
				if (!(stream >> function_type)) read_fail = true;
				else if (function_type < 0 || function_type >= 6) invalid_value = true;
				else {
					element.function = (NParamFunctionType) function_type;
					if (element.function == NParamFunctionType::CIPHER && !(stream >> element.function_internal_arg)) read_fail = true;
				}
			} else {
				debug(log_prefix + "nparam_c : invalid type : " + type);
				return false;
			}
			if (read_fail || invalid_value) {
				debug(log_prefix + "nparam_c : " + std::string(read_fail ? "Invalid line" : "Invalid value"));
				return false;
			} else nparam_proc.c.push_back(element);
		} else if (command == "nparam_op") {
			int func, arg0, arg1;
			if (!(stream >> func >> arg0 >> arg1)) {
				debug(log_prefix + "nparam_op : three integers expected");
				return false;
			}
			nparam_proc.ops.push_back({func, {arg0, arg1}});
		} else if (command == "end") return true;
		command_cnt++;
	}
	
	debug("[cache] No end");
	return false;
}

