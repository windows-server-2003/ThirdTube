#include "headers.hpp"
#include "settings.hpp"
#include "youtube_parser/parser.hpp"
#include "scenes/video_player.hpp"

void load_settings() {
	char buf[0x1001] = { 0 };
	u32 read_size;
	Result_with_string result = Util_file_load_from_file("settings.txt", DEF_MAIN_DIR, (u8 *) buf, 0x1000, &read_size);
	logger.info(DEF_SEM_INIT_STR , "Util_file_load_from_file()..." + result.string + result.error_description, result.code);
	auto settings = parse_xml_like_text(buf);
	
	auto load_int = [&] (std::string key, int default_value) {
		if (!settings.count(key)) return default_value;
		char *end;
		long res = strtol(settings[key].c_str(), &end, 10);
		if (*end) return default_value;
		res = std::min<long>(res, std::numeric_limits<int>::max());
		res = std::max<long>(res, std::numeric_limits<int>::min());
		return (int) res;
	};
	auto load_double = [&] (std::string key, double default_value) {
		if (!settings.count(key)) return default_value;
		char *end;
		double res = strtod(settings[key].c_str(), &end);
		if (*end) return default_value;
		return res;
	};
	auto load_string = [&] (std::string key, std::string default_value) {
		return settings.count(key) ? settings[key] : default_value;
	};
	var_lang = load_string("lang_ui", "en");
	if (var_lang != "en" && var_lang != "ja") var_lang = "en";
	var_lang_content = load_string("lang_content", "en");
	if (var_lang_content != "en" && var_lang_content != "ja") var_lang_content = "en";
	var_lcd_brightness = load_int("lcd_brightness", 100);
	if (var_lcd_brightness < 15 || var_lcd_brightness > 163) var_lcd_brightness = 100;
	var_time_to_turn_off_lcd = load_int("time_to_turn_off_lcd", 150);
	if (var_time_to_turn_off_lcd < 10) var_time_to_turn_off_lcd = 150;
	var_eco_mode = load_int("eco_mode", 1);
	var_full_screen_mode = load_int("full_screen_mode", 0);
	var_night_mode = load_int("dark_theme", 0);
	var_flash_mode = load_int("dark_theme_flash", 0);
	var_community_image_size = std::min(COMMUNITY_IMAGE_SIZE_MAX, std::max(COMMUNITY_IMAGE_SIZE_MIN, load_int("community_image_size", COMMUNITY_IMAGE_SIZE_DEFAULT)));
	var_autoplay_level = std::min(2, std::max(0, load_int("autoplay_level", 2)));
	var_forward_buffer_ratio = std::max(0.1, std::min(1.0, load_double("forward_buffer_ratio", 0.8)));
	var_history_enabled = load_int("history_enabled", 1);
	var_video_show_debug_info = load_int("video_show_debug_info", 0);
	var_video_linear_filter = load_int("linear_filter", 1);
	var_dpad_scroll_speed0 = std::max(1.0, std::min(12.0, load_double("dpad_scroll_speed0", 6.0)));
	var_dpad_scroll_speed1 = std::max(var_dpad_scroll_speed0, std::min(12.0, load_double("dpad_scroll_speed1", 9.0)));
	var_dpad_scroll_speed1_threashold = std::max(0.3, std::min(5.0, load_double("dpad_scroll_speed1_threashold", 2.0)));
	
	Util_cset_set_wifi_state(true);
	Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
	youtube_change_content_language(var_lang_content);
	video_set_linear_filter_enabled(var_video_linear_filter);
	video_set_show_debug_info(var_video_show_debug_info);
}
void save_settings() {
	std::string data;
	auto add_str = [&] (const std::string &key, const std::string &val) {
		data += "<" + key + ">" + val + "</" + key + ">\n";
	};
	auto add_int = [&] (const std::string &key, int val) {
		data += "<" + key + ">" + std::to_string(val) + "</" + key + ">\n";
	};
	auto add_double = [&] (const std::string &key, double val) {
		data += "<" + key + ">" + std::to_string(val) + "</" + key + ">\n";
	};
	
	add_str("lang_ui", var_lang);
	add_str("lang_content", var_lang_content);
	add_int("lcd_brightness", var_lcd_brightness);
	add_int("time_to_turn_off_lcd", var_time_to_turn_off_lcd);
	add_int("eco_mode", var_eco_mode);
	add_int("full_screen_mode", var_full_screen_mode);
	add_int("dark_theme", var_night_mode);
	add_int("dark_theme_flash", var_flash_mode);
	add_int("community_image_size", var_community_image_size);
	add_int("autoplay_level", var_autoplay_level);
	add_double("forward_buffer_ratio", var_forward_buffer_ratio);
	add_int("history_enabled", var_history_enabled);
	add_int("video_show_debug_info", var_video_show_debug_info);
	add_int("linear_filter", var_video_linear_filter);
	add_double("dpad_scroll_speed0", var_dpad_scroll_speed0);
	add_double("dpad_scroll_speed1", var_dpad_scroll_speed1);
	add_double("dpad_scroll_speed1_threashold", var_dpad_scroll_speed1_threashold);
	
	Result_with_string result = Util_file_save_to_file("settings.txt", DEF_MAIN_DIR, (u8 *) data.c_str(), data.size(), true);
	logger.info("settings/save", "Util_file_save_to_file()..." + result.string + result.error_description, result.code);
}
