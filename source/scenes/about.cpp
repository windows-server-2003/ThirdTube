#include <vector>
#include <string>
#include "headers.hpp"

#include "scenes/about.hpp"
#include "scenes/video_player.hpp"
#include "ui/ui_common.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "network/thumbnail_loader.hpp"

#define SMALL_MARGIN 3
#define DEFAULT_FONT_INTERVAL 13
#define MIDDLE_FONT_INTERVAL 18

namespace About {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
		
	const std::vector<std::string> app_description_lines = {
		"A work-in-progress homebrew YouTube client",
		"for the new 3DS"
	};
	const std::vector<std::pair<std::string, std::vector<std::string> > > credits = {
		{"Core 2 Extreme", {"for Video player for 3DS from which", "the video playback code of this app is taken"}}
	};
	const std::vector<std::string> license_lines = {
		"You can redistribute and/or modify this software",
		"under the terms of the GNU General Public",
		"License (GPL) v3 or under the terms of",
		"any later revisions of the GPL.",
	};
	const std::vector<std::pair<std::string, std::vector<std::string> > > third_party_licenses = {
		{"FFmpeg", {"by the FFmpeg developers under LGPLv2"} },
		{"json11", {"by Dropbox under MIT License"} },
		{"libctru", {"by devkitPro under zlib License"} },
		{"libcurl", {"by Daniel Stenberg and many contributors", "under the curl license"} },
		{"stb", {"by Sean Barrett under MIT License"} }
	};
	
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



static void draw_about() {
	int y_offset = -scroller.get_offset();
	{ // About section
		Draw("About", SMALL_MARGIN, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
		y_offset += MIDDLE_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		Draw_line(SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 320 - SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 1);
		y_offset += SMALL_MARGIN;
		Draw_x_centered("ThirdTube", 0, 320, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
		y_offset += MIDDLE_FONT_INTERVAL;
		Draw_x_centered("Version " + DEF_CURRENT_APP_VER, 0, 320, y_offset, 0.5, 0.5, LIGHT0_TEXT_COLOR);
		y_offset += DEFAULT_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		
		for (auto line : app_description_lines) {
			Draw_x_centered(line, 0, 320, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
		}
		
		Draw("GitHub", SMALL_MARGIN * 2, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		y_offset += DEFAULT_FONT_INTERVAL;
		Draw(GITHUB_URL, SMALL_MARGIN * 3, y_offset, 0.4, 0.4, DEFAULT_TEXT_COLOR);
		y_offset += DEFAULT_FONT_INTERVAL;
	}
	{ // Credits section
		y_offset += SMALL_MARGIN * 2;
		Draw("Credits", SMALL_MARGIN, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
		y_offset += MIDDLE_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		Draw_line(SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 320 - SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 1);
		y_offset += SMALL_MARGIN;
		for (auto i : credits) {
			Draw(i.first, SMALL_MARGIN * 2, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
			y_offset += MIDDLE_FONT_INTERVAL;
			for (auto line : i.second) {
				Draw(line, SMALL_MARGIN * 4, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
			}
		}
	}
	{ // License section
		y_offset += SMALL_MARGIN * 2;
		Draw("License", SMALL_MARGIN, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
		y_offset += MIDDLE_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		Draw_line(SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 320 - SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 1);
		y_offset += SMALL_MARGIN;
		for (auto line : license_lines) {
			Draw(line, SMALL_MARGIN, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
		}
	}
	{ // Thirdparty licenses
		y_offset += SMALL_MARGIN * 2;
		Draw("Third-party Licenses", SMALL_MARGIN, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
		y_offset += MIDDLE_FONT_INTERVAL;
		y_offset += SMALL_MARGIN;
		Draw_line(SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 320 - SMALL_MARGIN, std::round(y_offset), DEFAULT_TEXT_COLOR, 1);
		y_offset += SMALL_MARGIN;
		for (auto license : third_party_licenses) {
			Draw(license.first, SMALL_MARGIN, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += DEFAULT_FONT_INTERVAL;
			for (auto line : license.second) {
				Draw(line, SMALL_MARGIN * 3, y_offset, 0.5, 0.5, LIGHT0_TEXT_COLOR);
				y_offset += DEFAULT_FONT_INTERVAL;
			}
			y_offset += SMALL_MARGIN;
		}
	}
	scroller.draw_slider_bar();
}


Intent About_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::ABOUT);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	scroller.change_area(0, 320, 0, CONTENT_Y_HIGH);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		draw_about();
		
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
		// about section
		content_height += MIDDLE_FONT_INTERVAL * 2 + DEFAULT_FONT_INTERVAL * 3 + SMALL_MARGIN * 3 + app_description_lines.size() * DEFAULT_FONT_INTERVAL;
		// credits section
		content_height += MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 4;
		for (auto i : credits) content_height += (MIDDLE_FONT_INTERVAL + i.second.size() * DEFAULT_FONT_INTERVAL);
		// license section
		content_height += MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 4 + DEFAULT_FONT_INTERVAL * license_lines.size();
		// third-party licenses section
		content_height += MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 4;
		for (auto license : third_party_licenses) content_height += (DEFAULT_FONT_INTERVAL + SMALL_MARGIN) + DEFAULT_FONT_INTERVAL * license.second.size();
		// for the additional margin below the content
		content_height += SMALL_MARGIN * 2;
		
		auto released_point = scroller.update(key, content_height);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		// handle touches
		if (released_point.second != -1) do {
		} while (0);
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		
		static int consecutive_scroll = 0;
		if (key.h_c_up || key.h_c_down) {
			if (key.h_c_up) consecutive_scroll = std::max(0, consecutive_scroll) + 1;
			else consecutive_scroll = std::min(0, consecutive_scroll) - 1;
			
			float scroll_amount = DPAD_SCROLL_SPEED0;
			if (std::abs(consecutive_scroll) > DPAD_SCROLL_SPEED1_THRESHOLD) scroll_amount = DPAD_SCROLL_SPEED1;
			if (key.h_c_up) scroll_amount *= -1;
			
			scroller.scroll(scroll_amount);
			var_need_reflesh = true;
		} else consecutive_scroll = 0;
		
		if (key.h_touch || key.p_touch) var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
