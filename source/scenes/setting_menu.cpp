#include "headers.hpp"
#include <functional>
#include <regex>

#include "system/util/settings.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/video_player.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "youtube_parser/parser.hpp"
#include "system/util/history.hpp"
#include "system/util/misc_tasks.hpp"
#include "network/thumbnail_loader.hpp"
#include "network/network_io.hpp"
#include "json11/json11.hpp"

namespace Settings {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	bool is_3dsx = false;
	std::string path_3dsx;
	
	TextView *toast_view;
	int toast_frames_left = 0;
	
	OverlayView *popup_view;
	VerticalListView *main_view;
	TabView *main_tab_view;
	
	ProgressBarView *update_progress_bar_view;
	
	int CONTENT_Y_HIGH = 240;
	constexpr int TOP_HEIGHT = MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 2;
	
	constexpr int DIALOG_WIDTH = 240;
	
	// used for updating of the app
	enum class UpdateState {
		CHECKING_UPDATES,
		FAILED_CHECKING,
		UP_TO_DATE,
		UPDATES_AVAILABLE,
		INSTALLING,
		FAILED_INSTALLING,
		SUCCEEDED_INSTALLING
	};
	UpdateState update_state = UpdateState::CHECKING_UPDATES;
	
	static std::string update_error_message;
	
	static std::string update_url_cia;
	static std::string update_url_3dsx;
	static std::string next_version_str;
	static Handle resource_lock;
	static std::string install_progress_str;
	
	static Thread update_worker_thread;
	
	Thread settings_misc_thread;
};
using namespace Settings;



using namespace json11;

std::string install_update(NetworkSessionList &session_list) {
	std::string new_error_message = "";
	auto url = is_3dsx ? update_url_3dsx : update_url_cia;
	// auto url = update_url_cia;
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	install_progress_str = LOCALIZED(ACCESSING);
	svcReleaseMutex(resource_lock);
	update_progress_bar_view->set_progress(0);
	update_progress_bar_view->set_is_visible(true);
	
	bool first = true;
	auto result = session_list.perform(HttpRequest::GET(url, {}).with_progress_func([&] (u64 now, u64 total) {
		if (total < 10000) return; // ignore header(?)
		if (first) {
			first = false;
			svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
			install_progress_str = LOCALIZED(DOWNLOADING);
			svcReleaseMutex(resource_lock);
		}
		update_progress_bar_view->set_progress((double) now / total);
	}));
	
	if (result.fail) return "curl deep fail : " + result.error;
	if (result.status_code != 200) return "http returned " + std::to_string(result.status_code);
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	install_progress_str = LOCALIZED(INSTALLING);
	svcReleaseMutex(resource_lock);
	update_progress_bar_view->set_progress(1.0);
	
	Result libctru_result;
	if (is_3dsx) {
		char *slash_ptr = strrchr(path_3dsx.c_str(), '/');
		if (!slash_ptr) return "no slash in 3dsx path : " + path_3dsx;
		size_t slash_index = slash_ptr - path_3dsx.c_str();
		libctru_result = Util_file_save_to_file(path_3dsx.substr(slash_index + 1, path_3dsx.size()), path_3dsx.substr(0, slash_index + 1),
			result.data.data(), result.data.size(), true).code;
		if (libctru_result) return "Failed to write to .3dsx file : " + std::to_string(libctru_result);
	} else {
		Handle am_handle = 0;
		if ((libctru_result = AM_StartCiaInstall(MEDIATYPE_SD, &am_handle))) return "AM_StartCiaInstall() returned " + std::to_string(libctru_result);
		
		update_progress_bar_view->set_progress(0.0);
		
		const size_t BLOCK_SIZE = 200000;
		for (size_t i = 0; i < result.data.size(); ) {
			size_t size = std::min(BLOCK_SIZE, result.data.size() - i);
			u32 size_written;
			libctru_result = FSFILE_Write(am_handle, &size_written, i, result.data.data() + i, size, FS_WRITE_FLUSH);
			i += size_written;
			if (libctru_result) return "FSFILE_Write() returned " + std::to_string(libctru_result);
			update_progress_bar_view->set_progress((double) i / result.data.size());
		}
		update_progress_bar_view->set_progress(1.0);
		
		libctru_result = AM_FinishCiaInstall(am_handle);
		if (libctru_result) return "AM_FinishCiaInstall() returned " + std::to_string(libctru_result);
	}
	return "";
}

static void update_worker_thread_func(void *) {
	Util_log_save("updater", "Thread started.");
	
	static NetworkSessionList session_list;
	session_list.init();
	
	while (!exiting) {
		if (update_state == UpdateState::CHECKING_UPDATES) {
			std::string new_error_message;
			
			bool check_success = false;
			auto result = session_list.perform(HttpRequest::GET("https://api.github.com/repos/windows-server-2003/ThirdTube/releases/latest", {}));
			if (result.fail) new_error_message = "Failed accessing(deep fail) : " + result.fail;
			else if (result.status_code == 200) {
				std::string error;
				Json result_json = Json::parse(std::string(result.data.begin(), result.data.end()), error);
				if (error != "" || result_json == Json()) {
					Util_log_save("updater", "failed to parse json : " + error);
					new_error_message = "failed to parse json : " + error;
				} else {
					update_url_3dsx = update_url_cia;
					for (auto item : result_json["assets"].array_items()) {
						auto url = item["browser_download_url"].string_value();
						if (url.size() >= 5 && url.substr(url.size() - 5, 5) == ".3dsx") update_url_3dsx = url;
						if (url.size() >= 4 && url.substr(url.size() - 4, 4) == ".cia") update_url_cia = url;
					}
					if (update_url_cia == "") new_error_message = "could not find .cia url";
					if (update_url_3dsx == "") new_error_message = "could not find .3dsx url";
					if (update_url_cia != "" && update_url_3dsx != "") {
						// lexicographically compare the sequence of version numbers ([0, 4, 1] if v0.4.1)
						auto version_str_to_vector = [] (const std::string &str) {
							size_t head = 0;
							std::vector<int> res;
							while (head < str.size()) {
								while (head < str.size() && !isdigit(str[head])) head++;
								if (head >= str.size()) break;
								int cur = 0;
								while (head < str.size() && isdigit(str[head])) cur = cur * 10 + str[head++] - '0';
								res.push_back(cur);
							}
							return res;
						};
						auto new_version_seq = version_str_to_vector(result_json["tag_name"].string_value());
						auto cur_version_seq = version_str_to_vector(DEF_CURRENT_APP_VER);
						bool update_available = cur_version_seq < new_version_seq;
						if (update_available) update_state = UpdateState::UPDATES_AVAILABLE, next_version_str = result_json["tag_name"].string_value();
						else update_state = UpdateState::UP_TO_DATE;
						check_success = true;
					}
				}
			} else new_error_message = "http returned " + std::to_string(result.status_code);
			if (!check_success) update_state = UpdateState::FAILED_CHECKING;
			if (new_error_message != "") {
				svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
				update_error_message = new_error_message;
				svcReleaseMutex(resource_lock);
			}
		}
		if (update_state == UpdateState::INSTALLING) {
			std::string error = install_update(session_list);
			if (error == "") update_state = UpdateState::SUCCEEDED_INSTALLING;
			else {
				svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
				update_error_message = error;
				svcReleaseMutex(resource_lock);
				update_state = UpdateState::FAILED_INSTALLING;
			}
		}
		usleep(100000);
	}
	
	Util_log_save("updater", "Thread exit.");
	threadExit(0);
}



void Sem_init(void) {
	Util_log_save("settings/init", "Initializing...");
	Result_with_string result;
	
	load_settings();
	load_string_resources(var_lang);
	
	popup_view = new OverlayView(0, 0, 320, 240);
	popup_view->set_is_visible(false);
	toast_view = new TextView((320 - 150) / 2, 190, 150, DEFAULT_FONT_INTERVAL + SMALL_MARGIN);
	toast_view->set_is_visible(false);
	
	main_tab_view = (new TabView(0, 0, 320, CONTENT_Y_HIGH - TOP_HEIGHT))
		->set_stretch_subview(true)
		->set_views({
			// Tab #1 : UI/Display
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
					// Size of images in community posts
					(new BarView(0, 0, 320, 40))
						->set_values(COMMUNITY_IMAGE_SIZE_MIN, COMMUNITY_IMAGE_SIZE_MAX, var_community_image_size)
						->set_title([] (const BarView &view) { return LOCALIZED(COMMUNITY_POST_IMAGE_SIZE) + " : " + std::to_string(var_community_image_size) + " px"; })
						->set_while_holding([] (const BarView &view) {
							var_community_image_size = view.value;
							misc_tasks_request(TASK_CHANGE_BRIGHTNESS);
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					(new EmptyView(0, 0, 320, 10))
				}),
			// Tab #2 : Playback
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// Autoplay
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ONLY_IN_PLAYLIST); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_autoplay_level)
						->set_title([](const SelectorView &) { return LOCALIZED(AUTOPLAY); })
						->set_on_change([](const SelectorView &view) {
							if (var_autoplay_level != view.selected_button) {
								var_autoplay_level = view.selected_button;
								misc_tasks_request(TASK_CHANGE_BRIGHTNESS);
							}
						}),
				}),
			// Tab #3 : Data
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// History recording
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(DISABLED); },
							(std::function<std::string ()>) []() { return LOCALIZED(ENABLED); }
						}, var_history_enabled)
						->set_title([](const SelectorView &) { return LOCALIZED(WATCH_HISTORY); })
						->set_on_change([](const SelectorView &view) {
							if (var_history_enabled != view.selected_button) {
								var_history_enabled = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10)),
					// Erase history
					(new TextView(10, 0, 120, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text((std::function<std::string ()>) [] () { return LOCALIZED(REMOVE_ALL_HISTORY); })
						->set_x_alignment(TextView::XAlign::CENTER)
						->set_text_offset(0, -2)
						->set_get_background_color([] (const View &view) {
							int red = std::min<int>(0xFF, 0xD0 + 0x30 * view.touch_darkness);
							int other = 0x30 * (1 - view.touch_darkness);
							return 0xFF000000 | other << 8 | other << 16 | red;
						})
						->set_on_view_released([] (View &view) {
							popup_view->recursive_delete_subviews();
							popup_view->set_subview((new VerticalListView(0, 0, DIALOG_WIDTH))
								->set_views({
									(new TextView(0, 0, DIALOG_WIDTH, 30))
										->set_text_lines(split_string(LOCALIZED(REMOVE_ALL_HISTORY_CONFIRM), '\n'))
										->set_x_alignment(TextView::XAlign::CENTER)
										->set_text_offset(0, -1),
									(new HorizontalRuleView(0, 0, DIALOG_WIDTH, 1)),
									(new HorizontalListView(0, 0, 25))->set_views({
										(new TextView(0, 0, DIALOG_WIDTH / 2, 25))
											->set_text((std::function<std::string ()>) [] () { return LOCALIZED(CANCEL); })
											->set_text_offset(0, -1)
											->set_x_alignment(TextView::XAlign::CENTER)
											->set_get_background_color(View::STANDARD_BACKGROUND)
											->set_on_view_released([] (View &view) {
												popup_view->set_is_visible(false);
												var_need_reflesh = true;
											}),
										(new TextView(0, 0, DIALOG_WIDTH / 2, 25))
											->set_text((std::function<std::string ()>) [] () { return LOCALIZED(OK); })
											->set_text_offset(0, -1)
											->set_x_alignment(TextView::XAlign::CENTER)
											->set_get_background_color(View::STANDARD_BACKGROUND)
											->set_on_view_released([] (View &view) {
												history_erase_all();
												misc_tasks_request(TASK_SAVE_HISTORY);
												popup_view->set_is_visible(false);
												var_need_reflesh = true;
												
												float tabbed_content_y_high = CONTENT_Y_HIGH - main_tab_view->tab_selector_height;
												toast_view
													->set_text((std::function<std::string ()>) [] () { return LOCALIZED(ALL_HISTORY_REMOVED); })
													->set_x_alignment(TextView::XAlign::CENTER)
													->set_text_offset(0, -1)
													->set_get_text_color([] () { return (u32) -1; })
													->update_y_range(tabbed_content_y_high - 20, tabbed_content_y_high - 5)
													->set_get_background_color([] (const View &) { return 0x50000000; })
													->set_is_visible(true);
												toast_frames_left = 120;
											})
									})
								})
								->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; })
							);
							popup_view->set_is_visible(true);
							popup_view->set_on_cancel([] (OverlayView &view) {
								view.set_is_visible(false);
								var_need_reflesh = true;
							});
							var_need_reflesh = true;
						}),
					(new EmptyView(0, 0, 320, 10))
				}),
			// Tab #4 : Update
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text((std::function<std::string ()>) [] () { return LOCALIZED(UPDATE); })
						->set_font_size(0.6, DEFAULT_FONT_INTERVAL)
						->set_text_offset(0, -2),
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN))
						->set_text((std::function<std::string ()>) [] () -> std::string {
							if (update_state == UpdateState::CHECKING_UPDATES) return LOCALIZED(CHECKING_FOR_UPDATES);
							if (update_state == UpdateState::FAILED_CHECKING) return LOCALIZED(FAILED_CHECKING_UPDATES) + " : " + update_error_message;
							if (update_state == UpdateState::UP_TO_DATE) return LOCALIZED(APP_UP_TO_DATE);
							if (update_state == UpdateState::UPDATES_AVAILABLE) return LOCALIZED(UPDATES_AVAILABLE) + " : " + next_version_str;
							if (update_state == UpdateState::INSTALLING) return install_progress_str;
							if (update_state == UpdateState::FAILED_INSTALLING) return LOCALIZED(UPDATE_FAIL) + " : " + update_error_message;
							if (update_state == UpdateState::SUCCEEDED_INSTALLING) return LOCALIZED(UPDATE_SUCCESS);
							return "";
						}),
					(new EmptyView(0, 0, 320, SMALL_MARGIN)),
					(update_progress_bar_view = (new ProgressBarView(10, 0, 300, 5)))
						->set_get_color([] () { return DEF_DRAW_BLUE; })
						->set_is_visible(false),
					(new EmptyView(0, 0, 320, SMALL_MARGIN)),
					(new TextView(10, 0, 100, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text((std::function<std::string ()>) [] () -> std::string {
							if (update_state == UpdateState::FAILED_CHECKING) return LOCALIZED(RETRY);
							if (update_state == UpdateState::UPDATES_AVAILABLE) return LOCALIZED(UPDATE);
							if (update_state == UpdateState::FAILED_INSTALLING) return LOCALIZED(RETRY);
							return "";
						})
						->set_x_alignment(TextView::XAlign::CENTER)
						->set_text_offset(0, -1)
						->set_on_view_released([] (const View &) {
							if (update_state == UpdateState::FAILED_CHECKING) update_state = UpdateState::CHECKING_UPDATES;
							if (update_state == UpdateState::UPDATES_AVAILABLE || update_state == UpdateState::FAILED_INSTALLING) {
								std::vector<std::string> confirm_lines;
								if (is_3dsx) confirm_lines = truncate_str(std::regex_replace(LOCALIZED(OVERWRITE_3DSX_CONFIRM), std::regex("%0"), path_3dsx), DIALOG_WIDTH, 3, 0.5, 0.5);
								else confirm_lines = {LOCALIZED(INSTALL_CIA_CONFIRM)};
								
								popup_view->recursive_delete_subviews();
								popup_view->set_subview((new VerticalListView(0, 0, DIALOG_WIDTH))
									->set_views({
										(new TextView(0, 0, DIALOG_WIDTH, 45))
											->set_text_lines(confirm_lines)
											->set_x_alignment(TextView::XAlign::CENTER)
											->set_text_offset(0, -1),
										(new HorizontalRuleView(0, 0, DIALOG_WIDTH, 1)),
										(new HorizontalListView(0, 0, 25))->set_views({
											(new TextView(0, 0, DIALOG_WIDTH / 2, 25))
												->set_text((std::function<std::string ()>) [] () { return LOCALIZED(CANCEL); })
												->set_text_offset(0, -1)
												->set_x_alignment(TextView::XAlign::CENTER)
												->set_get_background_color(View::STANDARD_BACKGROUND)
												->set_on_view_released([] (View &view) {
													popup_view->set_is_visible(false);
													var_need_reflesh = true;
												}),
											(new TextView(0, 0, DIALOG_WIDTH / 2, 25))
												->set_text((std::function<std::string ()>) [] () { return LOCALIZED(OK); })
												->set_text_offset(0, -1)
												->set_x_alignment(TextView::XAlign::CENTER)
												->set_get_background_color(View::STANDARD_BACKGROUND)
												->set_on_view_released([] (View &view) {
													popup_view->set_is_visible(false);
													var_need_reflesh = true;
													update_state = UpdateState::INSTALLING;
												})
										})
									})
									->set_draw_order({0, 2, 1})
									->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; })
								);
								popup_view->set_is_visible(true);
								var_need_reflesh = true;
							}
						})
						->set_get_background_color([] (const View &view) -> u32 {
							if (update_state == UpdateState::FAILED_CHECKING || update_state == UpdateState::UPDATES_AVAILABLE) {
								int blue = std::min<int>(0xFF, 0xB0 + 0x30 * view.touch_darkness);
								int other = 0x50 + 0x20 * (1 - view.touch_darkness);
								return 0xFF000000 | blue << 16 | other << 8 | other;
							}
							return DEFAULT_BACK_COLOR;
						})
				}),
			// Tab #5 : Advanced
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
					// Use linear filter
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_video_linear_filter)
						->set_title([](const SelectorView &) { return LOCALIZED(LINEAR_FILTER); })
						->set_on_change([](const SelectorView &view) {
							if (var_video_linear_filter != view.selected_button) {
								var_video_linear_filter = view.selected_button;
								video_set_linear_filter_enabled(var_video_linear_filter);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10)),
					// Debug info in the control tab
					(new SelectorView(0, 0, 320, 35))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_video_show_debug_info)
						->set_title([](const SelectorView &view) { return LOCALIZED(VIDEO_SHOW_DEBUG_INFO); })
						->set_on_change([](const SelectorView &view) {
							if (var_video_show_debug_info != view.selected_button) {
								var_video_show_debug_info = view.selected_button;
								video_set_show_debug_info(var_video_show_debug_info);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10))
				})
		}, 0)
		->set_tab_texts({
			(std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS_DISPLAY_UI); },
			(std::function<std::string ()>) [] () { return LOCALIZED(PLAYBACK); },
			(std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS_DATA); },
			(std::function<std::string ()>) [] () { return LOCALIZED(UPDATE); },
			(std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS_ADVANCED); }
		});
	main_view = (new VerticalListView(0, 0, 320))
		->set_views({
			// 'Settings'
			(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS); })
				->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL)
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			// ---------------------
			(new HorizontalRuleView(0, 0, 320, SMALL_MARGIN * 2))
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			main_tab_view
		})
		->set_draw_order({2, 1, 0});
		
	
	is_3dsx = envIsHomebrew();
	if (is_3dsx) {
		const char *arglist = envGetSystemArgList();
		int argc = *(u32 *) arglist;
		if (argc) {
			arglist += 4;
			path_3dsx = arglist;
			if (path_3dsx.substr(0, 6) == "sdmc:/") path_3dsx = path_3dsx.substr(5, path_3dsx.size() - 5);
		}
	}
	svcCreateMutex(&resource_lock, false);
	
	update_worker_thread = threadCreate(update_worker_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_LOW, 1, false);
	
	Sem_resume("");
	already_init = true;
}
void Sem_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	save_settings();
	
	u64 time_out = 10000000000;
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(update_worker_thread, time_out));
	threadFree(update_worker_thread);
	
	svcCloseHandle(resource_lock);
	resource_lock = 0;
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	
	Util_log_save("settings/exit", "Exited.");
}
void Sem_suspend(void) {
	thread_suspend = true;
}
void Sem_resume(std::string arg) {
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}


Intent Sem_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	
	thumbnail_set_active_scene(SceneType::SETTINGS);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = 240;
	if (video_playing_bar_show) CONTENT_Y_HIGH -= VIDEO_PLAYING_BAR_HEIGHT;
	main_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		if (var_debug_mode) Draw_debug_info();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		main_view->draw();
		popup_view->draw();
		toast_view->draw();
		svcReleaseMutex(resource_lock);
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(CONTENT_Y_HIGH - main_tab_view->tab_selector_height - OVERLAY_MENU_ICON_SIZE);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	
	if (--toast_frames_left <= 0) toast_view->set_is_visible(false);

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key, &intent, SceneType::SETTINGS);
		
		// toast_view is never 'updated'
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		if (popup_view->is_visible) popup_view->update(key);
		else main_view->update(key);
		svcReleaseMutex(resource_lock);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		
		static int consecutive_scroll = 0;
		if (key.h_c_up || key.h_c_down) {
			if (key.h_c_up) consecutive_scroll = std::max(0, consecutive_scroll) + 1;
			else consecutive_scroll = std::min(0, consecutive_scroll) - 1;
			
			float scroll_amount = DPAD_SCROLL_SPEED0;
			if (std::abs(consecutive_scroll) > DPAD_SCROLL_SPEED1_THRESHOLD) scroll_amount = DPAD_SCROLL_SPEED1;
			if (key.h_c_up) scroll_amount *= -1;
			
			dynamic_cast<ScrollView *>(main_tab_view->views[main_tab_view->selected_tab])->scroll(scroll_amount);
			var_need_reflesh = true;
		} else consecutive_scroll = 0;
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		if (key.h_touch || key.p_touch) var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
