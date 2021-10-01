#include "headers.hpp"

static std::map<std::string, std::string> *string_resources = NULL;
static std::map<std::string, std::string> *string_resources_alt = NULL;


std::string get_string_resource(std::string id) {
	if (!string_resources) return "[SR Null Err]";
	if (!string_resources->count(id)) return "[SR Not Found]";
	return (*string_resources)[id];
}

Result_with_string load_string_resources(std::string lang) {
	Result_with_string result;
	
	static char buffer[0x2001];
	memset(buffer, 0, sizeof(buffer));
	u32 read_size;
	result = Util_file_load_from_rom("string_resources_" + lang + ".txt", "romfs:/gfx/msg/", (u8 *) buffer, 0x2000, &read_size);
	if (result.code != 0)
	{
		result.code = -1;
		result.error_description = "Failed to open the string resource file";
		return result;
	}
	
	std::map<std::string, std::string> *new_resources = new std::map<std::string, std::string>(parse_xml_like_text(buffer));
	if (!new_resources) {
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	for (auto &item : *new_resources) {
		std::string tmp_value;
		for (size_t i = 0; i < item.second.size(); ) {
			if (i + 1 < item.second.size() && item.second[i] == '\\' && item.second[i + 1] == 'n') tmp_value.push_back('\n'), i += 2;
			else tmp_value.push_back(item.second[i]), i++;
		}
		item.second = tmp_value;
	}
	// it's safe unless two load_string_resources() calls occur while the same call of get_string_resource() is running, which is highly unlikely
	delete(string_resources_alt);
	string_resources_alt = string_resources;
	string_resources = new_resources;
	
	var_need_reflesh = true;
	
	return result;
}


