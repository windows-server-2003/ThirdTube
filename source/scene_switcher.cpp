#include "headers.hpp"

#include "scenes/video_player.hpp"
#include "scenes/search.hpp"
#include "scenes/channel.hpp"
#include "scenes/about.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/watch_history.hpp"
#include "scenes/home.hpp"
#include "network/network_io.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/async_task.hpp"
#include "system/util/misc_tasks.hpp"
#include "ui/ui.hpp"
// add here


SceneType global_current_scene;
Intent global_intent;

namespace SceneSwitcher {
	static bool menu_thread_run = false;
	static bool menu_check_exit_request = false;
	static Thread menu_worker_thread, thumbnail_downloader_thread, async_task_thread, misc_tasks_thread;

	static void empty_thread(void* arg) { threadExit(0); }
	
	bool bot_screen_disabled = false;

	static Result sound_init_result;
};
using namespace SceneSwitcher;

void Menu_worker_thread(void* arg);

#define DECRYPTER_VERSION 0
#define DECRYPTER_URL ("https://raw.githubusercontent.com/windows-server-2003/ThirdTube/main/decrypter/" + std::to_string(DECRYPTER_VERSION) + "_latest.txt")
static void yt_load_decrypter(void *) {
	static NetworkSessionList session_list;
	static bool first = true;
	if (first) first = false, session_list.init();
	
	auto result = session_list.perform(HttpRequest::GET(DECRYPTER_URL, {}));
	if (result.fail) logger.error("yt-dec", "fail: " + result.error);
	else if (!result.status_code_is_success()) logger.error("yt-dec", "fail http: " + std::to_string(result.status_code));
	else {
		youtube_set_cipher_decrypter(std::string(result.data.begin(), result.data.end()));
		Util_file_save_to_file(std::to_string(DECRYPTER_VERSION) + "_decrypter.txt", DEF_MAIN_DIR, result.data.data(), result.data.size(), true);
	}
}

#define LOG_IF_ERROR(expr) \
	do {\
		auto res = expr;\
		if (res != 0) logger.error(DEF_MENU_INIT_STR, #expr ": " + std::to_string(res));\
	} while(0)

void Menu_init(void)
{
	Result_with_string result;
	
	logger.init();
	
	logger.info(DEF_MENU_INIT_STR, "Initializing..." + DEF_CURRENT_APP_VER);
	
	osSetSpeedupEnable(true);
	svcSetThreadPriority(CUR_THREAD_HANDLE, DEF_THREAD_PRIORITY_HIGH - 1);
	
	logger.info(DEF_MENU_INIT_STR, "Initializing services...");
	LOG_IF_ERROR(fsInit());
	LOG_IF_ERROR(acInit());
	LOG_IF_ERROR(aptInit());
	LOG_IF_ERROR(mcuHwcInit());
	LOG_IF_ERROR(ptmuInit());
	{
		constexpr int SOC_BUFFERSIZE = 0x100000;
		u32 *soc_buffer = (u32 *) memalign(0x1000, SOC_BUFFERSIZE);
		if (!soc_buffer) logger.error(DEF_MENU_INIT_STR, "soc buffer out of memory");
		else LOG_IF_ERROR(socInit(soc_buffer, SOC_BUFFERSIZE));
	}
	LOG_IF_ERROR(sslcInit(0));
	LOG_IF_ERROR(httpcInit(0x200000));
	LOG_IF_ERROR(romfsInit());
	LOG_IF_ERROR(cfguInit());
	LOG_IF_ERROR(amInit());
	LOG_IF_ERROR((sound_init_result = ndspInit())); // 0xd880A7FA if ndsp firmware is lacking
	LOG_IF_ERROR(APT_SetAppCpuTimeLimit(30));
	lock_network_state();
	
	aptSetSleepAllowed(false);
	set_apt_callback();
	
	logger.info(DEF_MENU_INIT_STR, "Services initialized.");
	
	
	APT_CheckNew3DS(&var_is_new3ds);
	CFGU_GetSystemModel(&var_model);
	
	if (var_is_new3ds) { // check core availability
		Thread core_2 = threadCreate(empty_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 2, false);
		var_core2_available = (bool) core_2;
		if (core_2) threadJoin(core_2, U64_MAX);
		threadFree(core_2);

		Thread core_3 = threadCreate(empty_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 3, false);
		var_core3_available = (bool) core_3;
		if (core_3) threadJoin(core_3, U64_MAX);
		threadFree(core_3);
	}
	
	LOG_IF_ERROR(Draw_init(var_model != CFG_MODEL_2DS).code);
	Draw_frame_ready();
	Draw_screen_ready(0, DEF_DRAW_WHITE);
	Draw_screen_ready(1, DEF_DRAW_WHITE);
	Draw_apply_draw();

	Util_expl_init();
	Exfont_init();
	for (int i = 0; i < DEF_EXFONT_NUM_OF_FONT_NAME; i++) Exfont_set_external_font_request_state(i, true);
	for(int i = 0; i < 4; i++) Exfont_set_system_font_request_state(i, true);

	Exfont_request_load_external_font();
	Exfont_request_load_system_font();
	
	menu_thread_run = true;
	menu_worker_thread = threadCreate(Menu_worker_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_REALTIME, 1, false);
	
	Sem_init();
	Sem_suspend();
	VideoPlayer_init();
	VideoPlayer_suspend();
	Channel_init();
	Channel_suspend();
	About_init();
	About_suspend();
	History_init();
	History_suspend();
	Search_init();
	Search_suspend();
	// add here
	Home_init(); // first running
	global_current_scene = SceneType::HOME;
	
	thumbnail_downloader_thread = threadCreate(thumbnail_downloader_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	async_task_thread = threadCreate(async_task_thread_func, NULL, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	misc_tasks_thread = threadCreate(misc_tasks_thread_func, NULL, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);

	Menu_get_system_info();
	
	// load default youtube decrypter
	u8 decrypter_buf[1001] = { 0 };
	u32 read_size;
	// first load from romfs, which is reliable
	result = Util_file_load_from_rom("yt_decrypter.txt", "romfs:/", decrypter_buf, 1000, &read_size);
	if (result.code != 0) logger.warning("yt-dec", "default fail: " + result.error_description);
	else youtube_set_cipher_decrypter((char *) decrypter_buf);
	// then try loading from local cache, which may be newer but does not always exist
	memset(decrypter_buf, 0, sizeof(decrypter_buf));
	result = Util_file_load_from_file(std::to_string(DECRYPTER_VERSION) + "_decrypter.txt", DEF_MAIN_DIR, decrypter_buf, 1000, &read_size);
	if (result.code != 0) logger.warning("yt-dec", "cache fail: " + result.error_description);
	else youtube_set_cipher_decrypter((char *) decrypter_buf);
	// fetch from remote
	queue_async_task(yt_load_decrypter, NULL);
	
	logger.info(DEF_MENU_INIT_STR, "Initialized.");
}

void Menu_exit(void)
{
	logger.info(DEF_MENU_EXIT_STR, "Exiting...");
	u64 time_out = 10000000000;
	Result_with_string result;

	menu_thread_run = false;

	VideoPlayer_exit();
	Channel_exit();
	Search_exit();
	Sem_exit();
	About_exit();
	History_exit();
	Home_exit();
	// add here

	Util_expl_exit();
	Exfont_exit();

	thumbnail_downloader_thread_exit_request();
	async_task_thread_exit_request();
	misc_tasks_thread_exit_request();
	NetworkSessionList::exit_request();
	unlock_network_state();
	
	remove_apt_callback();
	
	logger.info(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(menu_worker_thread, time_out));
	logger.info(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(thumbnail_downloader_thread, time_out));
	logger.info(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(async_task_thread, time_out));
	logger.info(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(misc_tasks_thread, time_out));
	threadFree(menu_worker_thread);
	threadFree(thumbnail_downloader_thread);
	threadFree(async_task_thread);
	threadFree(misc_tasks_thread);
	
	NetworkSessionList::at_exit();

	fsExit();
	acExit();
	aptExit();
	mcuHwcExit();
	ptmuExit();
	httpcExit();
	romfsExit();
	cfguExit();
	amExit();
	ndspExit();
	sslcExit();
	socExit();
	Draw_exit();

	logger.info(DEF_MENU_EXIT_STR, "Exited.");
}

static std::vector<Intent> scene_stack = {{SceneType::HOME, ""}};

bool Menu_main(void)
{
	Util_hid_update_key_state();
	
	Hid_info key;
	Util_hid_query_key_state(&key);
	
	if (sound_init_result != 0) {
		std::string error_msg = 
			"Could not initialize NDSP (sound service)\n"
			"This is probably because you haven't run DSP1.\n"
			"You can download it from the link below:\n\n"
			"https://github.com/zoogie/DSP1/releases/\n\n"
			"If you are on Luma3DS v11.0 or later, you\n"
			"can open Rosalina menu, go to Misc. options\n"
			" -> Dump DSP firmware instead.\n";
		error_msg += "\n\nPress A to close the app";
		
		
		Draw_frame_ready();
		Draw_screen_ready(0, DEF_DRAW_WHITE);
		
		Draw_xy_centered(error_msg, 0, 400, 0, 240, 0.5, 0.5, DEF_DRAW_BLACK);
		Draw_top_ui();

		Draw_screen_ready(1, DEF_DRAW_WHITE);
		
		Draw_apply_draw();
		
		static int frame_cnt = 0;
		if (++frame_cnt >= 30) {
			frame_cnt = 0;
			logger.info(DEF_MENU_INIT_STR, "ndspInit() retry...", (sound_init_result = ndspInit()));//0xd880A7FA
		}
		
		if (key.h_a) return false;
		return true;
	}
	
	if (var_show_fps) sprintf(var_status, "%02dfps %04d/%02d/%02d %02d:%02d:%02d ", (int)Draw_query_fps(), var_years, var_months, var_days, var_hours, var_minutes, var_seconds);
	else sprintf(var_status, "%04d/%02d/%02d %02d:%02d:%02d ", var_years, var_months, var_days, var_hours, var_minutes, var_seconds);
	
	if(var_debug_mode)
		var_need_reflesh = true;
	
	global_intent = Intent();
	if (global_current_scene == SceneType::VIDEO_PLAYER) VideoPlayer_draw();
	else if (global_current_scene == SceneType::SEARCH) Search_draw();
	else if (global_current_scene == SceneType::CHANNEL) Channel_draw();
	else if (global_current_scene == SceneType::SETTINGS) Sem_draw();
	else if (global_current_scene == SceneType::ABOUT) About_draw();
	else if (global_current_scene == SceneType::HISTORY) History_draw();
	else if (global_current_scene == SceneType::HOME) Home_draw();
	// add here
	
	if (global_intent.next_scene != SceneType::NO_CHANGE) {
		if (global_current_scene == SceneType::VIDEO_PLAYER) VideoPlayer_suspend();
		else if (global_current_scene == SceneType::SEARCH) Search_suspend();
		else if (global_current_scene == SceneType::CHANNEL) Channel_suspend();
		else if (global_current_scene == SceneType::SETTINGS) Sem_suspend();
		else if (global_current_scene == SceneType::ABOUT) About_suspend();
		else if (global_current_scene == SceneType::HISTORY) History_suspend();
		else if (global_current_scene == SceneType::HOME) Home_suspend();
		// add here
	}
	
	// common updates
	if (key.h_select && key.p_y) var_debug_mode = !var_debug_mode;
	if (key.h_select && key.h_r && key.p_a) var_show_fps = !var_show_fps;
	if (key.h_select && key.p_x) logger.draw_enabled ^= 1, var_need_reflesh = true; // toggle log drawing
	logger.update(key);
	if (key.h_touch || key.p_touch) var_need_reflesh = true;
	if (((key.h_select && key.p_start) || (key.h_start && key.p_select)) && var_model != CFG_MODEL_2DS) bot_screen_disabled = !bot_screen_disabled;
	
	
	if (global_intent.next_scene == SceneType::EXIT) return false;
	else if (global_intent.next_scene != SceneType::NO_CHANGE && global_intent != scene_stack.back()) {
		if (scene_stack.size() >= 2 && global_intent == scene_stack[scene_stack.size() - 2]) global_intent.next_scene = SceneType::BACK;
		if (global_intent.next_scene == SceneType::BACK) {
			if (scene_stack.size() >= 2) scene_stack.pop_back();
		} else scene_stack.push_back(global_intent);
		
		global_current_scene = scene_stack.back().next_scene;
		std::string arg = scene_stack.back().arg;
		
		if (global_current_scene == SceneType::VIDEO_PLAYER) VideoPlayer_resume(arg);
		else if (global_current_scene == SceneType::SEARCH) Search_resume(arg);
		else if (global_current_scene == SceneType::CHANNEL) Channel_resume(arg);
		else if (global_current_scene == SceneType::SETTINGS) Sem_resume(arg);
		else if (global_current_scene == SceneType::ABOUT) About_resume(arg);
		else if (global_current_scene == SceneType::HISTORY) History_resume(arg);
		else if (global_current_scene == SceneType::HOME) Home_resume(arg);
		// add here
	}
	
	return true;
}

void Menu_get_system_info(void)
{
	u8 battery_level = -1;
	u8 battery_voltage = -1;
	char* ssid = (char*)malloc(512);
	Result_with_string result;

	PTMU_GetBatteryChargeState(&var_battery_charge);//battery charge
	result.code = MCUHWC_GetBatteryLevel(&battery_level);//battery level(%)
	if(result.code == 0)
	{
		MCUHWC_GetBatteryVoltage(&battery_voltage);
		var_battery_voltage = 5.0 * (battery_voltage / 256); 
		var_battery_level_raw = battery_level;
	}
	else
	{
		PTMU_GetBatteryLevel(&battery_level);
		if ((int)battery_level == 0)
			var_battery_level_raw = 0;
		else if ((int)battery_level == 1)
			var_battery_level_raw = 5;
		else if ((int)battery_level == 2)
			var_battery_level_raw = 10;
		else if ((int)battery_level == 3)
			var_battery_level_raw = 30;
		else if ((int)battery_level == 4)
			var_battery_level_raw = 60;
		else if ((int)battery_level == 5)
			var_battery_level_raw = 100;
	}

	//ssid
	result.code = ACU_GetSSID(ssid);
	if(result.code == 0)
		var_connected_ssid = ssid;
	else
		var_connected_ssid = "";

	free(ssid);
	ssid = NULL;

	var_wifi_signal = osGetWifiStrength();
	//Get wifi state from shared memory #0x1FF81067
	var_wifi_state = *(u8 *) 0x1FF81067;
	if (var_wifi_state != 2) var_wifi_signal = 8;

	//Get time
	time_t unixTime = time(NULL);
	struct tm* timeStruct = gmtime((const time_t*)&unixTime);
	var_years = timeStruct->tm_year + 1900;
	var_months = timeStruct->tm_mon + 1;
	var_days = timeStruct->tm_mday;
	var_hours = timeStruct->tm_hour;
	var_minutes = timeStruct->tm_min;
	var_seconds = timeStruct->tm_sec;

	if (var_debug_mode)
	{
		//check free RAM
		var_free_ram = Menu_check_free_ram();
		var_free_linear_ram = linearSpaceFree();
	}
}

int Menu_check_free_ram(void)
{
	void *ptr[10000];
	int head = 0;
	int res = 0;
	
	int cur_size = 1000 * 1000;
	while (head < 10000 && cur_size >= 10000) {
		ptr[head] = malloc(cur_size);
		if (ptr[head]) res += cur_size, head++;
		else cur_size /= 10;
	}
	for (int i = 0; i < head; i++) free(ptr[i]);
	return res / 1000;
}


void Menu_worker_thread(void* arg)
{
	logger.info(DEF_MENU_WORKER_THREAD_STR, "Thread started.");
	int count = 0;
	Result_with_string result;
	
	while (menu_thread_run)
	{
		usleep(49000);
		count++;

		if (count >= 20)
		{
			Menu_get_system_info();
			var_need_reflesh = true;
			count = 0;
		}
		
		var_afk_time += 0.05;
		
		static bool cur_screen_on = true;
		static bool cur_screen_dimmed = false;
		static bool cur_bot_screen_disabled = false;
		bool next_screen_on;
		bool next_screen_dimmed = cur_screen_dimmed;
		bool next_bot_screen_disabled = bot_screen_disabled && !var_app_suspended;
		
		if (var_afk_time > var_time_to_turn_off_lcd) next_screen_on = false;
		else if(var_afk_time > std::max<float>(var_time_to_turn_off_lcd * 0.5, (var_time_to_turn_off_lcd - 10))) next_screen_on = true, next_screen_dimmed = true;
		else next_screen_on = true, next_screen_dimmed = false;
		
		if (cur_screen_on != next_screen_on || cur_screen_dimmed != next_screen_dimmed || cur_bot_screen_disabled != next_bot_screen_disabled) {
			// first handle on/off
			bool cur_top_on = cur_screen_on;
			bool cur_bot_on = cur_screen_on && !cur_bot_screen_disabled;
			bool next_top_on = next_screen_on;
			bool next_bot_on = next_screen_on && !next_bot_screen_disabled;
			if (cur_top_on == cur_bot_on && cur_top_on != next_top_on && cur_bot_on != next_bot_on) Util_cset_set_screen_state(true, true, next_top_on); // change both
			else {
				if (cur_top_on != next_top_on) Util_cset_set_screen_state(true, false, next_top_on);
				if (cur_bot_on != next_bot_on) Util_cset_set_screen_state(false, true, next_bot_on);
			}
			
			if (cur_screen_dimmed != next_screen_dimmed) Util_cset_set_screen_brightness(true, true, next_screen_dimmed ? 10 : var_lcd_brightness);
			cur_screen_on = next_screen_on;
			cur_screen_dimmed = next_screen_dimmed;
			cur_bot_screen_disabled = next_bot_screen_disabled;
		}

		if (var_flash_mode)
		{
			var_night_mode = !var_night_mode;
			var_need_reflesh = true;
		}
	}
	logger.info(DEF_MENU_WORKER_THREAD_STR, "Thread exit.");
	threadExit(0);
}
