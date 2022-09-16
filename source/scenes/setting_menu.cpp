#include "headers.hpp"
#include <functional>
#include <regex>

#include "system/util/settings.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/video_player.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "youtube_parser/parser.hpp"
#include "system/util/history.hpp"
#include "system/util/misc_tasks.hpp"
#include "network/thumbnail_loader.hpp"
#include "network/network_io.hpp"
#include "rapidjson_wrapper.hpp"

namespace Settings {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	bool is_3dsx = false;
	std::string path_3dsx;
	
	TextView *toast_view;
	int toast_frames_left = 0;
	
	OverlayDialogView *popup_view;
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
	static Mutex resource_lock;
	static std::string install_progress_str;
	
	static Thread update_worker_thread;
	
	Thread settings_misc_thread;
};
using namespace Settings;



std::string install_update(NetworkSessionList &session_list) {
	std::string new_error_message = "";
	auto url = is_3dsx ? update_url_3dsx : update_url_cia;
	// auto url = update_url_cia;
	
	resource_lock.lock();
	install_progress_str = LOCALIZED(ACCESSING);
	resource_lock.unlock();
	update_progress_bar_view->set_progress(0);
	update_progress_bar_view->set_is_visible(true);
	
	bool first = true;
	auto result = session_list.perform(HttpRequest::GET(url, {}).with_progress_func([&] (u64 now, u64 total) {
		if (total < 10000) return; // ignore header(?)
		if (first) {
			first = false;
			resource_lock.lock();
			install_progress_str = LOCALIZED(DOWNLOADING);
			resource_lock.unlock();
		}
		update_progress_bar_view->set_progress((double) now / total);
	}));
	
	if (result.fail) return "curl deep fail : " + result.error;
	if (result.status_code != 200) return "http returned " + std::to_string(result.status_code);
	
	resource_lock.lock();
	install_progress_str = LOCALIZED(INSTALLING);
	resource_lock.unlock();
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
	logger.info("updater", "Thread started.");
	
	static NetworkSessionList session_list;
	session_list.init();
	
	while (!exiting) {
		if (update_state == UpdateState::CHECKING_UPDATES) {
			std::string new_error_message;
			
			bool check_success = false;
			auto result = session_list.perform(HttpRequest::GET("https://api.github.com/repos/windows-server-2003/ThirdTube/releases/latest", {}));
			if (result.fail) new_error_message = "Failed accessing(deep fail) : " + result.fail;
			else if (result.status_code == 200) {
				result.data.push_back('\0');
				rapidjson::Document json_root;
				std::string error;
				RJson result_json = RJson::parse(json_root, (char *) result.data.data(), error);
				if (error != "") {
					logger.error("updater", "failed to parse json : " + error);
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
				resource_lock.lock();
				update_error_message = new_error_message;
				resource_lock.unlock();
			}
		}
		if (update_state == UpdateState::INSTALLING) {
			std::string error = install_update(session_list);
			if (error == "") update_state = UpdateState::SUCCEEDED_INSTALLING;
			else {
				resource_lock.lock();
				update_error_message = error;
				resource_lock.unlock();
				update_state = UpdateState::FAILED_INSTALLING;
			}
		}
		usleep(100000);
	}
	
	logger.info("updater", "Thread exit.");
	threadExit(0);
}



void Sem_init(void) {
	logger.info("settings/init", "Initializing...");
	Result_with_string result;
	
	load_settings();
	load_string_resources(var_lang);
	
	popup_view = new OverlayDialogView(0, 0, 320, 240);
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
						->set_values_sync(15, 163, &var_lcd_brightness)
						->set_title([] (const BarView &view) { return LOCALIZED(LCD_BRIGHTNESS); })
						->set_while_holding([] (const BarView &view) { misc_tasks_request(TASK_CHANGE_BRIGHTNESS); })
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Time to turn off LCD
					(new BarView(0, 0, 320, 40))
						->set_values(10, 310, var_time_to_turn_off_lcd <= 309 ? var_time_to_turn_off_lcd : 310)
						->set_title([] (const BarView &view) { return LOCALIZED(TIME_TO_TURN_OFF_LCD) + " : " +
							(view.get_value() <= 309 ? std::to_string((int) view.get_value()) + " " + LOCALIZED(SECONDS) : LOCALIZED(NEVER_TURN_OFF)); })
						->set_on_release([] (const BarView &view) {
							var_time_to_turn_off_lcd = view.get_value() <= 309 ? view.get_value() : 1000000000;
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
					// Scroll speed 0
					(new BarView(0, 0, 320, 35))
						->set_values_sync(DPAD_SCROLL_SPEED_MIN, DPAD_SCROLL_SPEED_MAX, &var_dpad_scroll_speed0)
						->set_title([] (const BarView &view) { return LOCALIZED(SCROLL_SPEED0) + " : " + double2str(var_dpad_scroll_speed0, 1); })
						->set_while_holding([] (const BarView &view) {
							var_dpad_scroll_speed1 = std::max(var_dpad_scroll_speed1, var_dpad_scroll_speed0);
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Scroll speed 1
					(new BarView(0, 0, 320, 35))
						->set_values_sync(DPAD_SCROLL_SPEED_MIN, DPAD_SCROLL_SPEED_MAX, &var_dpad_scroll_speed1)
						->set_title([] (const BarView &view) { return LOCALIZED(SCROLL_SPEED1) + " : " + double2str(var_dpad_scroll_speed1, 1); })
						->set_while_holding([] (const BarView &view) {
							var_dpad_scroll_speed0 = std::min(var_dpad_scroll_speed0, var_dpad_scroll_speed1);
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Scroll speed change threashold
					(new BarView(0, 0, 320, 40))
						->set_values_sync(DPAD_SCROLL_THREASHOLD_MIN, DPAD_SCROLL_THREASHOLD_MAX, &var_dpad_scroll_speed1_threashold)
						->set_title([] (const BarView &view) { return LOCALIZED(SCROLL_SPEED_THREASHOLD) + " : " + double2str(var_dpad_scroll_speed1_threashold, 1); })
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Size of images in community posts
					(new BarView(0, 0, 320, 40))
						->set_values_sync(COMMUNITY_IMAGE_SIZE_MIN, COMMUNITY_IMAGE_SIZE_MAX, &var_community_image_size)
						->set_title([] (const BarView &view) { return LOCALIZED(COMMUNITY_POST_IMAGE_SIZE) + " : " + std::to_string(var_community_image_size) + " px"; })
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
					// Forward buffer ratio
					(new BarView(0, 0, 320, 40))
						->set_values_sync(0.1, 1.0, &var_forward_buffer_ratio)
						->set_title([] (const BarView &view) {
							char ratio_str[16];
							snprintf(ratio_str, 16, "%.2f", var_forward_buffer_ratio);
							return LOCALIZED(FORWARD_BUFFER_RATIO) + " : " + ratio_str;
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); })
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
							popup_view->get_message_view()->set_text_lines(split_string(LOCALIZED(REMOVE_ALL_HISTORY_CONFIRM), '\n'))->update_y_range(0, 30);
							popup_view->set_buttons<std::function<std::string ()> >({
								[] () { return LOCALIZED(CANCEL); },
								[] () { return LOCALIZED(OK); }
							},
							[] (OverlayDialogView &, int button_pressed) {
								if (button_pressed == 1) {
									history_erase_all();
									misc_tasks_request(TASK_SAVE_HISTORY);
									
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
								}
								return true; // close the dialog
							});
							popup_view->set_is_visible(true);
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
								
								popup_view->get_message_view()->set_text_lines(confirm_lines)->update_y_range(0, 45);
								popup_view->set_buttons<std::function<std::string ()> >({
									[] () { return LOCALIZED(CANCEL); },
									[] () { return LOCALIZED(OK); }
								},
								[] (OverlayDialogView &, int button_pressed) {
									if (button_pressed == 1) update_state = UpdateState::INSTALLING;
									return true; // close the dialog
								});
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
		->set_tab_texts<std::function<std::string ()> >({
			[] () { return LOCALIZED(SETTINGS_DISPLAY_UI); },
			[] () { return LOCALIZED(PLAYBACK); },
			[] () { return LOCALIZED(SETTINGS_DATA); },
			[] () { return LOCALIZED(UPDATE); },
			[] () { return LOCALIZED(SETTINGS_ADVANCED); }
		});
	main_view = (new VerticalListView(0, 0, 320))
		->set_views({
			// 'Settings'
			(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS); })
				->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL)
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			// ---------------------
			(new RuleView(0, 0, 320, SMALL_MARGIN * 2))
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
	logger.info(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(update_worker_thread, time_out));
	threadFree(update_worker_thread);
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	
	logger.info("settings/exit", "Exited.");
}
void Sem_suspend(void) {
	thread_suspend = true;
}
void Sem_resume(std::string arg) {
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}


void Sem_draw(void)
{
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
		video_draw_top_screen();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		resource_lock.lock();
		main_view->draw();
		popup_view->draw();
		toast_view->draw();
		resource_lock.unlock();
		
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
		update_overlay_menu(&key);
		
		// toast_view is never 'updated'
		resource_lock.lock();
		if (popup_view->is_visible) popup_view->update(key);
		else main_view->update(key);
		resource_lock.unlock();
		
		if (video_playing_bar_show) video_update_playing_bar(key);
		
		if (key.p_b) global_intent.next_scene = SceneType::BACK;
	}
}
