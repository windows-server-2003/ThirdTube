#include "headers.hpp"

#include "system/util/settings.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/video_player.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"

#define SMALL_MARGIN 3
#define DEFAULT_FONT_INTERVAL 13
#define MIDDLE_FONT_INTERVAL 18

namespace Settings {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	int CONTENT_Y_HIGH = 240;
	
	volatile bool save_settings_request = false;
	volatile bool change_brightness_request = false;
	
	int last_touch_x = -1;
	int last_touch_y = -1;
	
	int bar_holding = -1; // 0 : LCD brightness, 1 : time to turn off LCD
	
	Thread settings_misc_thread;
	
	VerticalScroller scroller;
};
using namespace Settings;


static void settings_misc_thread_func(void *arg) {
	while (!exiting) {
		if (save_settings_request) {
			save_settings_request = false;
			save_settings();
		} else if (change_brightness_request) {
			change_brightness_request = false;
			Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
		} else usleep(50000);
	}
	
	Util_log_save("settings/save", "Thread exit.");
	threadExit(0);
}


bool Sem_query_init_flag(void) {
	return already_init;
}

void Sem_resume(std::string arg)
{
	scroller.on_resume();
	overlay_menu_on_resume();
	bar_holding = -1;
	thread_suspend = false;
	var_need_reflesh = true;
}

void Sem_suspend(void)
{
	thread_suspend = true;
}

void Sem_init(void)
{
	Util_log_save("settings/init", "Initializing...");
	Result_with_string result;
	
	load_settings();
	
	settings_misc_thread = threadCreate(settings_misc_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	
	// result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SEARCH_NUM_OF_MSG);
	// Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	Sem_resume("");
	already_init = true;
}

void Sem_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	u64 time_out = 10000000000;
	Util_log_save("settings", "threadJoin()...", threadJoin(settings_misc_thread, time_out));
	threadFree(settings_misc_thread);
	
	save_settings();
	
	Util_log_save("settings/exit", "Exited.");
}

void draw_settings_menu() {
	int y_offset = -scroller.get_offset();
	
	Draw("Settings", SMALL_MARGIN, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEF_DRAW_BLACK);
	y_offset += MIDDLE_FONT_INTERVAL;
	y_offset += SMALL_MARGIN;
	Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_BLACK, 320 - SMALL_MARGIN, y_offset, DEF_DRAW_BLACK, 1);
	y_offset += SMALL_MARGIN;
	{ // UI language
		Draw("UI Language", SMALL_MARGIN, y_offset, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += DEFAULT_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		int selected_x_l = var_lang == "en" ? 40 : 180;
		int selected_x_r = var_lang == "en" ? 140 : 280;
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, selected_x_l, y_offset, selected_x_r - selected_x_l, 20);
		Draw_x_centered("ja", 40, 140, y_offset + 2, 0.5, 0.5, DEF_DRAW_BLACK);
		Draw_x_centered("en", 180, 280, y_offset + 2, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += 20;
	}
	{ // Content language
		Draw("Content Language", SMALL_MARGIN, y_offset, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += DEFAULT_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		int selected_x_l = var_lang_content == "en" ? 40 : 180;
		int selected_x_r = var_lang_content == "en" ? 140 : 280;
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, selected_x_l, y_offset, selected_x_r - selected_x_l, 20);
		Draw_x_centered("ja", 40, 140, y_offset + 2, 0.5, 0.5, DEF_DRAW_BLACK);
		Draw_x_centered("en", 180, 280, y_offset + 2, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += 20;
	}
	{ // LCD Brightness
		Draw("LCD Brightness", SMALL_MARGIN, y_offset, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += DEFAULT_FONT_INTERVAL;
		y_offset += SMALL_MARGIN * 3;
		float bar_x_l = 40;
		float bar_x_r = 280;
		float x = bar_x_l + (bar_x_r - bar_x_l) * (var_lcd_brightness - 15) / (163 - 15);
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, bar_x_l, y_offset, bar_x_r - bar_x_l, 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, bar_x_l, y_offset, x - bar_x_l, 3);
		C2D_DrawCircleSolid(x, y_offset + 1, 0, bar_holding == 0 ? 6 : 4, DEF_DRAW_WEAK_AQUA);
		
		y_offset += SMALL_MARGIN * 3;
	}
	{ // Time to turn off the LCD
		Draw("Time to turn off the LCD : " + std::to_string(var_time_to_turn_off_lcd) + " s", SMALL_MARGIN, y_offset, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += DEFAULT_FONT_INTERVAL;
		y_offset += SMALL_MARGIN * 3;
		float bar_x_l = 40;
		float bar_x_r = 280;
		float x = bar_x_l + (bar_x_r - bar_x_l) * (var_time_to_turn_off_lcd - 10) / (310 - 10);
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, bar_x_l, y_offset, bar_x_r - bar_x_l, 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, bar_x_l, y_offset, x - bar_x_l, 3);
		C2D_DrawCircleSolid(x, y_offset + 1, 0, bar_holding == 1 ? 6 : 4, DEF_DRAW_WEAK_AQUA);
		
		y_offset += SMALL_MARGIN * 3;
	}
	{ // Eco mode
		Draw("Eco mode", SMALL_MARGIN, y_offset, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += DEFAULT_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		int selected_x_l = !var_eco_mode ? 40 : 180;
		int selected_x_r = !var_eco_mode ? 140 : 280;
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, selected_x_l, y_offset, selected_x_r - selected_x_l, 20);
		Draw_x_centered("Off", 40, 140, y_offset + 3, 0.5, 0.5, DEF_DRAW_BLACK);
		Draw_x_centered("On", 180, 280, y_offset + 3, 0.5, 0.5, DEF_DRAW_BLACK);
		y_offset += 20;
	}
}


Intent Sem_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	int color = DEF_DRAW_BLACK;
	int back_color = DEF_DRAW_WHITE;
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	if (var_night_mode)
	{
		color = DEF_DRAW_WHITE;
		back_color = DEF_DRAW_BLACK;
	}
	
	thumbnail_set_active_scene(SceneType::SETTINGS);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	scroller.change_area(0, 320, 0, CONTENT_Y_HIGH);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, back_color);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, back_color);
		
		draw_settings_menu();
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT : 240 - OVERLAY_MENU_ICON_SIZE);
		scroller.draw_slider_bar();
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key, &intent, SceneType::SETTINGS);
		
		int content_height = 0;
		auto released_point = scroller.update(key, content_height);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		// handle touches
		/*
		if (released_point.second != -1) do {
		} while (0);*/
		if (key.p_touch) {
			int y_offset = -scroller.get_offset();
			y_offset += MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 2;
			y_offset += DEFAULT_FONT_INTERVAL + SMALL_MARGIN;
			if (key.touch_y >= y_offset && key.touch_y < y_offset + 20) {
				auto prev_value = var_lang;
				if (key.touch_x >= 40 && key.touch_x < 140) var_lang = "en";
				if (key.touch_x >= 180 && key.touch_x < 280) var_lang = "ja";
				if (var_lang != prev_value) save_settings_request = true;
			}
			y_offset += 20;
			y_offset += DEFAULT_FONT_INTERVAL + SMALL_MARGIN;
			if (key.touch_y >= y_offset && key.touch_y < y_offset + 20) {
				auto prev_value = var_lang_content;
				if (key.touch_x >= 40 && key.touch_x < 140) var_lang_content = "en";
				if (key.touch_x >= 180 && key.touch_x < 280) var_lang_content = "ja";
				if (var_lang_content != prev_value) save_settings_request = true;
			}
			y_offset += 20;
			y_offset += DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 3;
			if (key.touch_y >= y_offset - 5 && key.touch_y <= y_offset + 3 + 5 && key.touch_x >= 40 - 5 && key.touch_y <= 280 + 5) {
				bar_holding = 0;
			}
			y_offset += SMALL_MARGIN * 3;
			y_offset += DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 3;
			if (key.touch_y >= y_offset - 5 && key.touch_y <= y_offset + 3 + 5 && key.touch_x >= 40 - 5 && key.touch_y <= 280 + 5) {
				bar_holding = 1;
			}
			y_offset += SMALL_MARGIN * 3;
			y_offset += DEFAULT_FONT_INTERVAL + SMALL_MARGIN;
			if (key.touch_y >= y_offset && key.touch_y < y_offset + 20) {
				auto prev_value = var_eco_mode;
				if (key.touch_x >= 40 && key.touch_x < 140) var_eco_mode = false;
				if (key.touch_x >= 180 && key.touch_x < 280) var_eco_mode = true;
				if (var_eco_mode != prev_value) save_settings_request = true;
			}
			y_offset += 20;
		}
		if (last_touch_x != -1 && key.touch_x == -1 && bar_holding != -1) {
			save_settings_request = true;
			bar_holding = -1;
			var_need_reflesh = true;
		}
		if (bar_holding == 0) {
			var_lcd_brightness = 15 + (163 - 15) * std::max(0.0f, std::min<float>(1.0f,  (float) (key.touch_x - 40) / (280 - 40)));
			change_brightness_request = true;
		} else if (bar_holding == 1) {
			var_time_to_turn_off_lcd = 10 + (310 - 10) * std::max(0.0f, std::min<float>(1.0f, (float) (key.touch_x - 40) / (280 - 40)));
		}
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		if (key.p_x) load_settings();
		
		last_touch_x = key.touch_x;
		last_touch_y = key.touch_y;
		
		if (key.h_touch || key.p_touch) var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
