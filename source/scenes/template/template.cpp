#include "headers.hpp"

#include "scenes/about.hpp"
#include "scenes/video_player.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"

namespace About {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	int CONTENT_Y_HIGH = 240;
	
	VerticalScroller scroller;
};
using namespace About;


bool About_query_init_flag(void) {
	return already_init;
}

void About_resume(std::string arg)
{
	scroller.on_resume();
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}

void About_suspend(void)
{
	thread_suspend = true;
}

void About_init(void)
{
	Util_log_save("about/init", "Initializing...");
	Result_with_string result;
	
	// result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SEARCH_NUM_OF_MSG);
	// Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	About_resume("");
	already_init = true;
}

void About_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	Util_log_save("about/exit", "Exited.");
}


Intent About_draw(void)
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
	
	thumbnail_set_active_scene(SceneType::ABOUT);
	
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
		
		Draw("About", 0, 0, 0.5, 0.5, DEF_DRAW_BLACK);
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT : 240 - OVERLAY_MENU_ICON_SIZE);
		
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
		update_overlay_menu(&key, &intent, SceneType::ABOUT);
		
		int content_height = 0;
		auto released_point = scroller.update(key, content_height);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		// handle touches
		if (released_point.second != -1) do {
		} while (0);
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		
		if (key.h_touch || key.p_touch) var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
