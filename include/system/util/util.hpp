#pragma once
#include <map>
#include <string>
#include "types.hpp"

Result_with_string Util_parse_file(std::string source_data, int num_of_items, std::string out_data[]);

std::string Util_convert_seconds_to_time(double input_seconds);

std::string Util_encode_to_escape(std::string in_data);

std::map<std::string, std::string> parse_xml_like_text(std::string data);
