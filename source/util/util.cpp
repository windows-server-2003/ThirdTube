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

std::vector<std::string> split_string(const std::string &str, char splitter) {
	std::vector<std::string> res = {""};
	for (auto c : str) {
		if (c == splitter) res.push_back("");
		else res.back().push_back(c);
	}
	return res;
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
// assumes input_str doesn't contain any linebreaks
std::vector<std::string> truncate_str(std::string input_str, int max_width, int max_lines, double x_size, double y_size) {
	u32 input[1024];
	int n = Extfont_parse_utf8_str_to_u32(input_str.c_str(), input, 1024);
	
	std::vector<std::vector<u64> > words; // each word is considered not separable
	std::vector<size_t> word_start;
	for (int i = 0; i < n; i++) {
		bool seperate;
		if (!i) seperate = true;
		else {
			u64 last_char = words.back().back();
			// either last_char or next char is multibyte or whitespace
			seperate = (last_char >> 8) || (input[i] >> 8) || (last_char == (u8) ' ') || (input[i] == (u8) ' ');
		}
		if (seperate) {
			words.push_back({input[i]});
			word_start.push_back(i);
		} else words.back().push_back(input[i]);
	}
	word_start.push_back(n);
	
	int m = words.size();
	int head = 0;
	std::vector<std::string> res;
	for (int line = 0; line < max_lines; line++) {
		if (head >= m) break;
		
		int fit_word_num = 0;
		float cur_line_width = 0;
		{ // get the number of words that fit in the line
			for (int i = head; i < m; i++) {
				float cur_word_width = 0;
				for (u64 c : words[i]) cur_word_width += Draw_get_width_one(c, x_size);
				if (cur_line_width + cur_word_width <= max_width) {
					fit_word_num = i - head + 1;
					cur_line_width += cur_word_width;
				} else break;
			}
		}
		
		std::string cur_line;
		for (size_t i = word_start[head]; i < word_start[head + fit_word_num]; i++) {
			u64 cur_char = input[i];
			if (cur_char >> 24) cur_line.push_back(cur_char >> 24);
			if (cur_char >> 16) cur_line.push_back(cur_char >> 16 & 0xFF);
			if (cur_char >> 8) cur_line.push_back(cur_char >> 8 & 0xFF);
			cur_line.push_back(cur_char & 0xFF);
		}
		
		bool force_fit = !fit_word_num || (line == max_lines - 1 && head + fit_word_num < m);
		if (force_fit) {
			const std::vector<u64> &cur_word = words[head + fit_word_num];
			
			int max_fit = 0;
			float width_left = max_width - cur_line_width;
			width_left -= 3 * Draw_get_width_one('.', x_size);
			
			if (width_left >= 0) {
				for (size_t i = 0; i < cur_word.size(); i++) {
					float cur_size = Draw_get_width_one(cur_word[i], x_size);
					if (width_left >= cur_size) {
						width_left -= cur_size;
						max_fit = i + 1;
					} else break;
				}
			}
			for (int i = 0; i < max_fit; i++) {
				u64 cur_char = cur_word[i];
				if (cur_char >> 24) cur_line.push_back(cur_char >> 24);
				if (cur_char >> 16) cur_line.push_back(cur_char >> 16 & 0xFF);
				if (cur_char >> 8) cur_line.push_back(cur_char >> 8 & 0xFF);
				cur_line.push_back(cur_char & 0xFF);
			}
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

