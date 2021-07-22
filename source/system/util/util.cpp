#include "headers.hpp"

Result_with_string Util_parse_file(std::string source_data, int num_of_items, std::string out_data[])
{
	Result_with_string result;

	size_t parse_start_num = 0;
	size_t parse_end_num = 0;
	std::string parse_start_text;
	std::string parse_end_text;

	for (int i = 0; i < num_of_items; i++)
	{
		parse_start_text = "<" + std::to_string(i) + ">";
		parse_start_num = source_data.find(parse_start_text);
		parse_end_text = "</" + std::to_string(i) + ">";
		parse_end_num = source_data.find(parse_end_text);

		if (parse_end_num == std::string::npos || parse_start_num == std::string::npos)
		{
			result.code = -1;
			result.string = "[Error] Failed to parse file. error pos : " + std::to_string(i) + " ";
			break;
		}

		parse_start_num += parse_start_text.length();
		parse_end_num -= parse_start_num;
		out_data[i] = source_data.substr(parse_start_num, parse_end_num);
	}

	return result;
}

std::string Util_convert_seconds_to_time(double input_seconds)
{
	if (input_seconds == INFINITY) return "INF"; // for debug purpose
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	long count = 0;
	std::string time = "";
	
	if(std::isnan(input_seconds) || std::isinf(input_seconds))
		input_seconds = 0;
	
	count = (long) input_seconds;
	seconds = count % 60;
	minutes = count / 60 % 60;
	hours = count / 3600;

	if(hours != 0)
		time += std::to_string(hours) + ":";

	if(minutes < 10)
		time += "0" + std::to_string(minutes) + ":";
	else
		time += std::to_string(minutes) + ":";

	if(seconds < 10)
		time += "0" + std::to_string(seconds);
	else
		time += std::to_string(seconds);

	// time += std::to_string(input_seconds - count + 1).substr(1, 2);
	return time;
}


std::string Util_encode_to_escape(std::string in_data)
{
	int string_length = in_data.length();
	std::string check;
	std::string return_data = "";

	for(int i = 0; i < string_length; i++)
	{
		check = in_data.substr(i, 1);
		if(check == "\n")
			return_data += "\\n";
		else if(check == "\u0022")
			return_data += "\\\u0022";
		else if(check == "\u005c")
			return_data += "\\\u005c";
		else
			return_data += in_data.substr(i, 1);
	}

	return return_data;
}

std::map<std::string, std::string> parse_xml_like_text(std::string data) {
	int head = 0;
	int n = data.size();
	std::map<std::string, std::string> res;
	while (head < n) {
		while (head < n && data[head] != '<') head++;
		head++;
		if (head >= n) break;
		std::string key;
		while (head < n && data[head] != '>') key.push_back(data[head++]);
		head++;
		if (head >= n) break;
		
		std::string closing_pattern = "</" + key + ">";
		auto closing_pos = data.find(closing_pattern, head);
		if (closing_pos == std::string::npos) break;
		std::string value = data.substr(head, closing_pos - head);
		head = closing_pos + closing_pattern.size();
		
		res[key] = value;
	}
	return res;
}

