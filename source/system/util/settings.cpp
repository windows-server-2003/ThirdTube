#include "headers.hpp"
#include "youtube_parser/parser.hpp"
#include "scenes/video_player.hpp"

void load_settings() {
	char buf[0x1001] = { 0 };
	u32 read_size;
	Result_with_string result = Util_file_load_from_file("settings.txt", DEF_MAIN_DIR, (u8 *) buf, 0x1000, &read_size);
	Util_log_save(DEF_SEM_INIT_STR , "Util_file_load_from_file()..." + result.string + result.error_description, result.code);
	auto settings = parse_xml_like_text(buf);
	
	for (auto i : settings) Util_log_save("settings/load", i.first + ":" + i.second);
	
	auto load_int = [&] (std::string key, int default_value) {
		if (!settings.count(key)) return default_value;
		char *end;
		long res = strtol(settings[key].c_str(), &end, 10);
		if (*end) return default_value;
		res = std::min<long>(res, std::numeric_limits<int>::max());
		res = std::max<long>(res, std::numeric_limits<int>::min());
		return (int) res;
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
	var_network_framework = var_network_framework_changed = load_int("use_experimental_sslc", -1); // for back compability
	if (var_network_framework < 0 || var_network_framework >= 3) var_network_framework = var_network_framework_changed = load_int("network_framework", -1);
	if (var_network_framework < 0 || var_network_framework >= 3) var_network_framework = var_network_framework_changed = 2;
	var_history_enabled = load_int("history_enabled", 1);
	var_video_show_debug_info = load_int("video_show_debug_info", 0);
	var_video_linear_filter = load_int("linear_filter", 1);
	
	Util_cset_set_wifi_state(true);
	Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
	youtube_change_content_language(var_lang_content);
	video_set_linear_filter_enabled(var_video_linear_filter);
	video_set_show_debug_info(var_video_show_debug_info);
}
void save_settings() {
	std::string data = 
		"<lang_ui>" + var_lang + "</lang_ui>\n" +
		"<lang_content>" + var_lang_content + "</lang_content>\n" +
		"<lcd_brightness>" + std::to_string(var_lcd_brightness) + "</lcd_brightness>\n" +
		"<time_to_turn_off_lcd>" + std::to_string(var_time_to_turn_off_lcd) + "</time_to_turn_off_lcd>\n" +
		"<eco_mode>" + std::to_string(var_eco_mode) + "</eco_mode>\n" + 
		"<full_screen_mode>" + std::to_string(var_full_screen_mode) + "</full_screen_mode>\n" +
		"<dark_theme>" + std::to_string(var_night_mode) + "</dark_theme>\n" + 
		"<dark_theme_flash>" + std::to_string(var_flash_mode) + "</dark_theme_flash>\n" + 
		"<network_framework>" + std::to_string(var_network_framework_changed) + "</network_framework>\n" +
		"<history_enabled>" + std::to_string(var_history_enabled) + "</history_enabled>\n" +
		"<video_show_debug_info>" + std::to_string(var_video_show_debug_info) + "</video_show_debug_info>\n" +
		"<linear_filter>" + std::to_string(var_video_linear_filter) + "</linear_filter>\n";
	
	Result_with_string result = Util_file_save_to_file("settings.txt", DEF_MAIN_DIR, (u8 *) data.c_str(), data.size(), true);
	Util_log_save("settings/save", "Util_file_save_to_file()..." + result.string + result.error_description, result.code);
}
