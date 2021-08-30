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

// truncate and wrap into at most `max_lines` lines so that each line fit in `max_width` if drawn with the size of `x_size` x `y_size`
std::vector<std::string> truncate_str(std::string input_str, int max_width, int max_lines, double x_size, double y_size) {
	static std::string input[1024 + 1];
	int n;
	Exfont_text_parse(input_str, &input[0], 1024, &n);
	
	std::vector<std::vector<std::string> > words; // each word is considered not separable
	for (int i = 0; i < n; i++) {
		bool seperate;
		if (!i) seperate = true;
		else {
			std::string last_char = words.back().back();
			seperate = last_char.size() != 1 || input[i].size() != 1 || last_char == " " || input[i] == " ";
		}
		if (seperate) words.push_back({input[i]});
		else words.back().push_back(input[i]);
	}
	
	int m = words.size();
	int head = 0;
	std::vector<std::string> res;
	for (int line = 0; line < max_lines; line++) {
		if (head >= m) break;
		
		int fit_word_num = 0;
		{ // binary search the number of words that fit in the line
			int l = 0;
			int r = std::min(50, m - head + 1);
			while (r - l > 1) {
				int m = l + ((r - l) >> 1);
				std::string query_text;
				for (int i = head; i < head + m; i++) for (auto j : words[i]) query_text += j;
				if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
				else r = m;
			}
			fit_word_num = l;
		}
		
		std::string cur_line;
		for (int i = head; i < head + fit_word_num; i++) for (auto j : words[i]) cur_line += j;
		bool force_fit = !fit_word_num || (line == max_lines - 1 && fit_word_num < m - head);
		if (force_fit) {
			std::vector<std::string> cur_word = words[head + fit_word_num];
			int l = 0;
			int r = cur_word.size();
			while (r - l > 1) { // binary search the number of characters that fit in the first line
				int m = l + ((r - l) >> 1);
				std::string query_text = cur_line;
				for (int i = 0; i < m; i++) query_text += cur_word[i];
				query_text += "...";
				if (Draw_get_width(query_text, x_size, y_size) <= max_width) l = m;
				else r = m;
			}
			for (int i = 0; i < l; i++) cur_line += cur_word[i];
			cur_line += "...";
			res.push_back(cur_line);
			head += fit_word_num + 1;
		} else {
			res.push_back(cur_line);
			head += fit_word_num;
		}
	}
	if (!res.size()) res = {""};
	return res;
}

