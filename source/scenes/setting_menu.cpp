#include "headers.hpp"
#include <functional>

#include "system/util/settings.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/video_player.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "youtube_parser/parser.hpp"
#include "system/util/misc_tasks.hpp"
#include "network/thumbnail_loader.hpp"

namespace Settings {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	VerticalListView *main_view;
	TabView *main_tab_view;
	
	int CONTENT_Y_HIGH = 240;
	constexpr int TOP_HEIGHT = MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 2;
	
	Thread settings_misc_thread;
};
using namespace Settings;


bool Sem_query_init_flag(void) {
	return already_init;
}

void Sem_resume(std::string arg)
{
	overlay_menu_on_resume();
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
	load_string_resources(var_lang);
	
	main_tab_view = (new TabView(0, 0, 320, CONTENT_Y_HIGH - TOP_HEIGHT))
		->set_stretch_subview(true)
		->set_views({
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// UI language
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(LANG_EN); },
							(std::function<std::string ()>) []() { return LOCALIZED(LANG_JA); }
						}, var_lang == "ja" ? 1 : 0)
						->set_title([](const SelectorView &) { return LOCALIZED(UI_LANGUAGE); })
						->set_on_change([](const SelectorView &view) {
							auto next_lang = std::vector<std::string>{"en", "ja"}[view.selected_button];
							if (var_lang != next_lang) {
								var_lang = next_lang;
								misc_tasks_request(TASK_RELOAD_STRING_RESOURCE);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Content language
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(LANG_EN); },
							(std::function<std::string ()>) []() { return LOCALIZED(LANG_JA); }
						}, var_lang_content == "ja" ? 1 : 0)
						->set_title([](const SelectorView &) { return LOCALIZED(CONTENT_LANGUAGE); })
						->set_on_change([](const SelectorView &view) {
							auto next_lang = std::vector<std::string>{"en", "ja"}[view.selected_button];
							if (var_lang_content != next_lang) {
								var_lang_content = next_lang;
								misc_tasks_request(TASK_SAVE_SETTINGS);
								youtube_change_content_language(var_lang_content);
							}
						}),
					// LCD Brightness
					(new BarView(0, 0, 320, 40))
						->set_values(15, 163, var_lcd_brightness)
						->set_title([] (const BarView &view) { return LOCALIZED(LCD_BRIGHTNESS); })
						->set_while_holding([] (const BarView &view) {
							var_lcd_brightness = view.value;
							misc_tasks_request(TASK_CHANGE_BRIGHTNESS);
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Time to turn off LCD
					(new BarView(0, 0, 320, 40))
						->set_values(10, 310, var_time_to_turn_off_lcd <= 309 ? var_time_to_turn_off_lcd : 310)
						->set_title([] (const BarView &view) { return LOCALIZED(TIME_TO_TURN_OFF_LCD) + " : " +
							(view.value <= 309 ? std::to_string((int) view.value) + " " + LOCALIZED(SECONDS) : LOCALIZED(NEVER_TURN_OFF)); })
						->set_on_release([] (const BarView &view) {
							var_time_to_turn_off_lcd = view.value <= 309 ? view.value : 1000000000;
							misc_tasks_request(TASK_SAVE_SETTINGS);
						}),
					// full screen mode
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_full_screen_mode)
						->set_title([](const SelectorView &) { return LOCALIZED(FULL_SCREEN_MODE); })
						->set_on_change([](const SelectorView &view) {
							if (var_full_screen_mode != view.selected_button) {
								var_full_screen_mode = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Dark theme (plus flash)
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); },
							(std::function<std::string ()>) []() { return LOCALIZED(FLASH); }
						}, var_flash_mode ? 2 : var_night_mode)
						->set_title([](const SelectorView &) { return LOCALIZED(DARK_THEME); })
						->set_on_change([](const SelectorView &view) {
							if (var_flash_mode != (view.selected_button == 2)) {
								var_flash_mode = (view.selected_button == 2);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
							if (!var_flash_mode && var_night_mode != view.selected_button) {
								var_night_mode = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10))
				}),
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// Eco mode
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_eco_mode)
						->set_title([](const SelectorView &) { return LOCALIZED(ECO_MODE); })
						->set_on_change([](const SelectorView &view) {
							if (var_eco_mode != view.selected_button) {
								var_eco_mode = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Network framework
					(new SelectorView(0, 0, 320, 35))
						->set_texts({"httpc", "sslc", "libcurl"}, var_network_framework_changed)
						->set_title([](const SelectorView &view) { return LOCALIZED(NETWORK_FRAMEWORK) +
							(var_network_framework != var_network_framework_changed ? " (" + LOCALIZED(RESTART_TO_APPLY) + ")" : ""); })
						->set_on_change([](const SelectorView &view) {
							var_network_framework_changed = view.selected_button;
							misc_tasks_request(TASK_SAVE_SETTINGS);
						}),
					(new EmptyView(0, 0, 320, 10))
				})
		}, 0)
		->set_tab_texts({
			(std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS_DISPLAY_UI); },
			(std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS_ADVANCED); }
		});
	main_view = (new VerticalListView(0, 0, 320))
		->set_views({
			// 'Settings'
			(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS); })
				->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL)
				->set_background_color((u32) -1),
			// ---------------------
			(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
				->set_background_color((u32) -1),
			main_tab_view
		})
		->set_draw_order({2, 1, 0});
		
		
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
	
	save_settings();
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	
	Util_log_save("settings/exit", "Exited.");
}

Intent Sem_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::SETTINGS);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		main_view->draw();
		
		if (video_playing_bar_show) video_draw_playing_bar();
		float overlay_menu_y = 240 - OVERLAY_MENU_ICON_SIZE;
		if (video_playing_bar_show) overlay_menu_y -= VIDEO_PLAYING_BAR_HEIGHT;
		overlay_menu_y -= main_tab_view->tab_selector_height;
		draw_overlay_menu(overlay_menu_y);
		
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
		
		main_view->update(key);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		if (key.h_touch || key.p_touch) var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
