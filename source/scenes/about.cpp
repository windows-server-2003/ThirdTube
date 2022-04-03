#include <vector>
#include <string>
#include "headers.hpp"

#include "scenes/about.hpp"
#include "scenes/video_player.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "network/thumbnail_loader.hpp"

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
		{"rapidjson", {"by Tencent and Milo Yip under MIT license"}},
		{"Duktape", {"by Duktape authors under MIT License"} },
		{"libctru", {"by devkitPro under zlib License"} },
		{"libcurl", {"by Daniel Stenberg and many contributors", "under the curl license"} },
		{"libbrotli", {"by the Brotli Authors under the MIT license"} },
		{"stb", {"by Sean Barrett under MIT License"} }
	};
	
	int CONTENT_Y_HIGH = 240;
	
	ScrollView *main_view = NULL;
};
using namespace About;

void About_init(void) {
	Util_log_save("about/init", "Initializing...");
	
	VerticalListView *credits_view = new VerticalListView(0, 0, 320);
	for (auto i : credits) {
		credits_view->views.push_back((new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text(i.first)
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL));
		credits_view->views.push_back((new TextView(SMALL_MARGIN * 2, 0, 320, DEFAULT_FONT_INTERVAL * i.second.size()))
			->set_text_lines(i.second));
	}
	
	VerticalListView *third_party_licenses_view = new VerticalListView(0, 0, 320);
	for (auto license : third_party_licenses) {
		third_party_licenses_view->views.push_back((new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))->set_text(license.first));
		third_party_licenses_view->views.push_back((new TextView(SMALL_MARGIN * 2, 0, 320, DEFAULT_FONT_INTERVAL * license.second.size()))
			->set_text_lines(license.second));
	}
	
	main_view = (new ScrollView(0, 0, 320, 240))->set_views({
		// About section
		(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text("About")
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
		(new RuleView(0, 0, 320, SMALL_MARGIN * 2)),
		(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text("ThirdTube")
			->set_x_alignment(TextView::XAlign::CENTER)
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
		(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
			->set_text("Version " + DEF_CURRENT_APP_VER)
			->set_get_text_color([] () { return LIGHT0_TEXT_COLOR; })
			->set_x_alignment(TextView::XAlign::CENTER),
		(new EmptyView(0, 0, 320, SMALL_MARGIN)),
		(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * app_description_lines.size()))
			->set_text_lines(app_description_lines)
			->set_x_alignment(TextView::XAlign::CENTER),
		(new TextView(SMALL_MARGIN, 0, 320, DEFAULT_FONT_INTERVAL))
			->set_text("GitHub"),
		(new TextView(SMALL_MARGIN * 2, 0, 320, DEFAULT_FONT_INTERVAL))
			->set_text(GITHUB_URL)
			->set_font_size(0.44, DEFAULT_FONT_INTERVAL),
		(new EmptyView(0, 0, 320, SMALL_MARGIN * 2)),
		
		// Credits section
		(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text("Credits")
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
		(new RuleView(0, 0, 320, SMALL_MARGIN * 2)),
		credits_view,
		(new EmptyView(0, 0, 320, SMALL_MARGIN * 2)),
		
		// License section
		(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text("License")
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
		(new RuleView(0, 0, 320, SMALL_MARGIN * 2)),
		(new TextView(SMALL_MARGIN, 0, 320, DEFAULT_FONT_INTERVAL * license_lines.size()))
			->set_text_lines(license_lines),
		(new EmptyView(0, 0, 320, SMALL_MARGIN * 2)),
		
		// Thirdparty licenses section
		(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
			->set_text("Third-party Licenses")
			->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
		(new RuleView(0, 0, 320, SMALL_MARGIN * 2)),
		third_party_licenses_view,
		(new EmptyView(0, 0, 320, SMALL_MARGIN * 2)),
	});
	
	About_resume("");
	already_init = true;
}
void About_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	
	Util_log_save("about/exit", "Exited.");
}
void About_suspend(void) { thread_suspend = true; }
void About_resume(std::string arg) {
	overlay_menu_on_resume();
	main_view->reset_holding_status();
	
	thread_suspend = false;
	var_need_reflesh = true;
}

Intent About_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	
	thumbnail_set_active_scene(SceneType::ABOUT);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_view->update_y_range(0, CONTENT_Y_HIGH);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		video_draw_top_screen();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		main_view->draw();
		
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
		
		main_view->update(key);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
	}
	
	return intent;
}
