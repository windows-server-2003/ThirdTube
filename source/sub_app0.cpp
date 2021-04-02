#include "headers.hpp"

#include "sub_app0.hpp"

bool vid_main_run = false;
bool vid_thread_run = false;
bool vid_already_init = false;
bool vid_thread_suspend = true;
bool vid_play_request = false;
bool vid_change_video_request = false;
bool vid_convert_request = false;
bool vid_wait_request = false;
bool vid_detail_mode = false;
bool vid_pause_request = false;
bool vid_seek_request = false;
bool vid_linear_filter = true;
bool vid_show_controls = false;
bool vid_allow_skip_frames = false;
double vid_time[6][320];
double vid_frametime = 0;
double vid_framerate = 0;
double vid_duration = 0;
double vid_zoom = 1;
double vid_x = 0;
double vid_y = 15;
double vid_current_pos = 0;
double vid_seek_pos = 0;
int vid_width = 0;
int vid_height = 0;
int vid_tex_width[4] = { 0, 0, 0, 0, };
int vid_tex_height[4] = { 0, 0, 0, 0, };
int vid_lr_count = 0;
int vid_cd_count = 0;
std::string vid_file = "";
std::string vid_dir = "";
std::string vid_video_format = "n/a";
std::string vid_audio_format = "n/a";
std::string vid_msg[DEF_SAPP0_NUM_OF_MSG];
Image_data vid_image[8];
Thread vid_decode_thread, vid_convert_thread;

void Sapp0_callback(std::string file, std::string dir)
{
	vid_file = file;
	vid_dir = dir;
	vid_change_video_request = true;
}

void Sapp0_cancel(void)
{

}

bool Sapp0_query_init_flag(void)
{
	return vid_already_init;
}

bool Sapp0_query_running_flag(void)
{
	return vid_main_run;
}

void Sapp0_decode_thread(void* arg)
{
	Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Thread started.");

	Result_with_string result;
	int bitrate = 0;
	int sample_rate = 0;
	int ch = 0;
	int audio_size = 0;
	int w = 0;
	int h = 0;
	int sleep = 0;
	int skip = 0;
	double pos = 0;
	bool key = false;
	bool has_audio = false;
	bool has_video = false;
	u8* audio = NULL;
	std::string format = "";
	std::string type = "";
	TickCounter counter[4];
	osTickCounterStart(&counter[0]);
	osTickCounterStart(&counter[1]);
	osTickCounterStart(&counter[2]);
	osTickCounterStart(&counter[3]);

	while (vid_thread_run)
	{
		if(vid_play_request || vid_change_video_request)
		{
			sleep = 0;
			skip = 0;
			vid_x = 0;
			vid_y = 15;
			vid_frametime = 0;
			vid_framerate = 0;
			vid_current_pos = 0;
			vid_duration = 0;
			vid_zoom = 1;
			vid_width = 0;
			vid_height = 0;
			vid_video_format = "n/a";
			vid_audio_format = "n/a";
			vid_change_video_request = false;
			vid_play_request = true;

			for(int i = 0; i < 4; i++)
			{
				vid_tex_width[i] = 0;
				vid_tex_height[i] = 0;
			}

			for(int i = 0 ; i < 320; i++)
			{
				vid_time[0][i] = 0;
				vid_time[1][i] = 0;
				vid_time[2][i] = 0;
				vid_time[3][i] = 0;
				vid_time[4][i] = 0;
				vid_time[5][i] = 0;
			}

			result = Util_decoder_open_file(vid_dir + vid_file, &has_audio, &has_video, 1);
			Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_decoder_open_file()..." + result.string + result.error_description, result.code);
			if(result.code != 0)
				vid_play_request = false;

			if(has_audio && vid_play_request)
			{
				result = Util_audio_decoder_init(1);
				Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_audio_decoder_init()..." + result.string + result.error_description, result.code);
				if(result.code != 0)
					vid_play_request = false;

				if(vid_play_request)
				{
					Util_audio_decoder_get_info(&bitrate, &sample_rate, &ch, &vid_audio_format, &vid_duration, 1);
					Util_speaker_init(1, ch, sample_rate);
				}
			}
			if(has_video && vid_play_request)
			{
				result = Util_video_decoder_init(0, 1);
				Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_video_decoder_init()..." + result.string + result.error_description, result.code);
				if(result.code != 0)
					vid_play_request = false;
				
				if(vid_play_request)
				{
					Util_video_decoder_get_info(&vid_width, &vid_height, &vid_framerate, &vid_video_format, &vid_duration, 1);
					vid_frametime = (1000.0 / vid_framerate);

					//fit to screen size
					while(((vid_width * vid_zoom) > 400 || (vid_height * vid_zoom) > 225) && vid_zoom > 0.05)
						vid_zoom -= 0.001;

					vid_x = (400 - (vid_width * vid_zoom)) / 2;
					vid_y = (225 - (vid_height * vid_zoom)) / 2;
					vid_y += 15;

					if(vid_width % 16 != 0)
						vid_width += 16 - vid_width % 16;
					/*if(vid_width >= 420 && vid_width <= 431)
						vid_width = 432;
					if(vid_width >= 436 && vid_width <= 447)
						vid_width = 448;
					if(vid_width >= 452 && vid_width <= 463)
						vid_width = 464;
					if(vid_width >= 468 && vid_width <= 479)
						vid_width = 480;
					if(vid_width == 644)
						vid_width = 656;*/
				}
			}

			if(result.code != 0)
			{
				Util_err_set_error_message(result.string, result.error_description, DEF_SAPP0_DECODE_THREAD_STR, result.code);
				Util_err_set_error_show_flag(true);
				var_need_reflesh = true;
			}

			osTickCounterUpdate(&counter[2]);
			while(vid_play_request)
			{
				result = Util_decoder_read_packet(&type, 1);
				if(result.code != 0)
					Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_decoder_read_packet()..." + result.string + result.error_description, result.code);
				
				var_afk_time = 0;

				if(vid_pause_request)
				{
					Util_speaker_pause(1);
					while(vid_pause_request && vid_play_request && !vid_seek_request && !vid_change_video_request)
						usleep(20000);
					
					Util_speaker_resume(1);
				}

				if(vid_seek_request)
				{
					//µs
					result = Util_decoder_seek(vid_seek_pos * 1000, 8, 1);
					Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_decoder_seek()..." + result.string + result.error_description, result.code);
					Util_speaker_clear_buffer(1);
					vid_seek_request = false;
				}

				if(!vid_play_request || vid_change_video_request || result.code != 0)
					break;

				if(type == "audio")
				{
					result = Util_decoder_ready_audio_packet(1);
					if(result.code == 0)
					{
						osTickCounterUpdate(&counter[1]);
						result = Util_audio_decoder_decode(&audio_size, &audio, &pos, 1);
						osTickCounterUpdate(&counter[1]);
						vid_time[1][319] = osTickCounterRead(&counter[1]);
						for(int i = 1; i < 320; i++)
							vid_time[1][i - 1] = vid_time[1][i];

						if(!has_video)
							vid_current_pos = pos;
						
						if(result.code == 0)
						{
							while(true)
							{
								result = Util_speaker_add_buffer(1, ch, audio, audio_size);
								if(result.code == 0 || !vid_play_request || vid_seek_request || vid_change_video_request)
									break;
								
								usleep(3000);
							}
						}
						else
							Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_audio_decoder_decode()..." + result.string + result.error_description, result.code);

						free(audio);
						audio = NULL;
					}
					else
						Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_decoder_ready_audio_packet()..." + result.string + result.error_description, result.code);
				}
				else if(type == "video")
				{
					if(vid_allow_skip_frames && skip > vid_frametime)
					{
						skip -= vid_frametime;
						Util_decoder_skip_video_packet(1);
					}
					else
					{
						result = Util_decoder_ready_video_packet(1);

						if(result.code == 0)
						{
							while(vid_wait_request)
								usleep(1000);

							osTickCounterUpdate(&counter[0]);
							result = Util_video_decoder_decode(&w, &h, &key, &pos, 1);
							osTickCounterUpdate(&counter[0]);
							vid_time[0][319] = osTickCounterRead(&counter[0]);
							for(int i = 1; i < 320; i++)
								vid_time[0][i - 1] = vid_time[0][i];

							vid_current_pos = pos;
							if(result.code == 0)
								vid_convert_request = true;
							else
								Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_video_decoder_decode()..." + result.string + result.error_description, result.code);

							osTickCounterUpdate(&counter[2]);
							vid_time[4][319] = osTickCounterRead(&counter[2]);
							for(int i = 1; i < 320; i++)
								vid_time[4][i - 1] = vid_time[4][i];
							
							if(vid_frametime - vid_time[4][319] > 0)
							{
								sleep += vid_frametime - vid_time[4][319];
								if(sleep > vid_frametime * 3)
								{
									usleep(sleep / 2 * 1000);
									sleep -= (sleep / 2);
								}
							}
							else if(vid_allow_skip_frames)
								skip -= vid_frametime - vid_time[4][319];

							osTickCounterUpdate(&counter[2]);
						}
						else
							Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Util_decoder_ready_video_packet()..." + result.string + result.error_description, result.code);
					}
				}
			}

			vid_convert_request = false;
			while(vid_wait_request)
				usleep(10000);

			if(has_audio)
			{
				Util_audio_decoder_exit(1);
				while(Util_speaker_is_playing(1))
					usleep(10000);
				
				Util_speaker_exit(1);
			}
			if(has_video)
				Util_video_decoder_exit(1);

			vid_pause_request = false;
			vid_seek_request = false;
			if(!vid_change_video_request)
				vid_play_request = false;
		}
		else
			usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);

		while (vid_thread_suspend)
			usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
	}

	Util_log_save(DEF_SAPP0_DECODE_THREAD_STR, "Thread exit.");
	threadExit(0);
}

void Sapp0_convert_thread(void* arg)
{
	Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Thread started.");
	u8* yuv_video = NULL;
	u8* video = NULL;
	TickCounter counter[3];
	Result_with_string result;

	APT_SetAppCpuTimeLimit(80);
	osTickCounterStart(&counter[0]);
	osTickCounterStart(&counter[1]);
	osTickCounterStart(&counter[2]);

	while (vid_thread_run)
	{	
		if(vid_play_request)
		{
			while(vid_play_request)
			{
				if(vid_convert_request)
				{
					vid_wait_request = true;
					osTickCounterUpdate(&counter[2]);
					result = Util_video_decoder_get_image(&yuv_video, vid_width, vid_height, 1);
					
					vid_convert_request = false;
					vid_wait_request = false;
					if(result.code == 0)
					{
						osTickCounterUpdate(&counter[0]);
						result = Util_converter_yuv420p_to_bgr565(yuv_video, &video, vid_width, vid_height);
						osTickCounterUpdate(&counter[0]);
						vid_time[2][319] = osTickCounterRead(&counter[0]);
						for(int i = 1; i < 320; i++)
							vid_time[2][i - 1] = vid_time[2][i];

						if(result.code == 0)
						{
							osTickCounterUpdate(&counter[1]);
							if(vid_width > 1024 && vid_height > 1024)
							{
								vid_tex_width[0] = 1024;
								vid_tex_width[1] = vid_width - 1024;
								vid_tex_width[2] = 1024;
								vid_tex_width[3] = vid_width - 1024;
								vid_tex_height[0] = 1024;
								vid_tex_height[1] = 1024;
								vid_tex_height[2] = vid_height - 1024;
								vid_tex_height[3] = vid_height - 1024;
							}
							else if(vid_width > 1024)
							{
								vid_tex_width[0] = 1024;
								vid_tex_width[1] = vid_width - 1024;
								vid_tex_height[0] = vid_height;
								vid_tex_height[1] = vid_height;
							}
							else if(vid_height > 1024)
							{
								vid_tex_width[0] = vid_width;
								vid_tex_width[1] = vid_width;
								vid_tex_height[0] = 1024;
								vid_tex_height[1] = vid_height - 1024;
							}
							else
							{
								vid_tex_width[0] = vid_width;
								vid_tex_height[0] = vid_height;
							}

							result = Draw_set_texture_data(&vid_image[0], video, vid_width, vid_height, 1024, 1024, GPU_RGB565);
							if(result.code != 0)
								Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);

							if(vid_width > 1024)
							{
								result = Draw_set_texture_data(&vid_image[1], video, vid_width, vid_height, 1024, 0, 1024, 1024, GPU_RGB565);
								if(result.code != 0)
									Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
							}
							if(vid_height > 1024)
							{
								result = Draw_set_texture_data(&vid_image[2], video, vid_width, vid_height, 0, 1024, 1024, 1024, GPU_RGB565);
								if(result.code != 0)
									Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
							}
							if(vid_width > 1024 && vid_height > 1024)
							{
								result = Draw_set_texture_data(&vid_image[3], video, vid_width, vid_height, 1024, 1024, 1024, 1024, GPU_RGB565);
								if(result.code != 0)
									Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Draw_set_texture_data()..." + result.string + result.error_description, result.code);
							}

							osTickCounterUpdate(&counter[1]);
							vid_time[3][319] = osTickCounterRead(&counter[1]);
							for(int i = 1; i < 320; i++)
								vid_time[3][i - 1] = vid_time[3][i];
							
							free(yuv_video);
							free(video);
							yuv_video = NULL;
							video = NULL;
							var_need_reflesh = true;
						}
						else
							Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Util_converter_yuv420p_to_bgr565()..." + result.string + result.error_description, result.code);
					}
					else
						Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Util_video_decoder_get_image()..." + result.string + result.error_description, result.code);

					osTickCounterUpdate(&counter[2]);
					vid_time[5][319] = osTickCounterRead(&counter[2]);
					for(int i = 1; i < 320; i++)
						vid_time[5][i - 1] = vid_time[5][i];
				}
				else
					usleep(1000);
			}
		}
		else
			usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);

		while (vid_thread_suspend)
			usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
	}
	
	Util_log_save(DEF_SAPP0_CONVERT_THREAD_STR, "Thread exit.");
	threadExit(0);
}

void Sapp0_resume(void)
{
	vid_thread_suspend = false;
	vid_main_run = true;
	var_need_reflesh = true;
	Menu_suspend();
}

void Sapp0_suspend(void)
{
	vid_thread_suspend = true;
	vid_main_run = false;
	Menu_resume();
}

void Sapp0_init(void)
{
	Util_log_save(DEF_SAPP0_INIT_STR, "Initializing...");
	bool new_3ds = false;
	Result_with_string result;
	
	APT_CheckNew3DS(&new_3ds);
	vid_thread_run = true;
	if(new_3ds)
	{
		vid_decode_thread = threadCreate(Sapp0_decode_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 2, false);
		vid_convert_thread = threadCreate(Sapp0_convert_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	}
	else
	{
		vid_decode_thread = threadCreate(Sapp0_decode_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 1, false);
		vid_convert_thread = threadCreate(Sapp0_convert_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	}

	for(int i = 0; i < 4; i++)
	{
		vid_tex_width[i] = 0;
		vid_tex_height[i] = 0;
	}

	for(int i = 0 ; i < 320; i++)
	{
		vid_time[0][i] = 0;
		vid_time[1][i] = 0;
		vid_time[2][i] = 0;
		vid_time[3][i] = 0;
		vid_time[4][i] = 0;
		vid_time[5][i] = 0;
	}

	for(int i = 0 ; i < 4; i++)
	{
		result = Draw_c2d_image_init(&vid_image[i], 1024, 1024, GPU_RGB565);
		if(result.code != 0)
		{
			Util_err_set_error_message(DEF_ERR_OUT_OF_LINEAR_MEMORY_STR, "", DEF_SAPP0_INIT_STR, DEF_ERR_OUT_OF_LINEAR_MEMORY);
			Util_err_set_error_show_flag(true);
			vid_thread_run = false;
		}
	}

	vid_detail_mode = false;
	vid_show_controls = false;
	vid_allow_skip_frames = false;
	vid_lr_count = 0;
	vid_cd_count = 0;
	vid_x = 0;
	vid_y = 15;
	vid_frametime = 0;
	vid_framerate = 0;
	vid_current_pos = 0;
	vid_duration = 0;
	vid_zoom = 1;
	vid_width = 0;
	vid_height = 0;
	vid_file = "";
	vid_dir = "";
	vid_video_format = "n/a";
	vid_audio_format = "n/a";

	result = Util_load_msg("sapp0_" + var_lang + ".txt", vid_msg, DEF_SAPP0_NUM_OF_MSG);
	Util_log_save(DEF_SAPP0_INIT_STR, "Util_load_msg()..." + result.string + result.error_description, result.code);

	Sapp0_resume();
	vid_already_init = true;
	Util_log_save(DEF_SAPP0_INIT_STR, "Initialized.");
}

void Sapp0_exit(void)
{
	Util_log_save(DEF_SAPP0_EXIT_STR, "Exiting...");
	u64 time_out = 10000000000;
	Result_with_string result;

	vid_already_init = false;
	vid_thread_suspend = false;
	vid_thread_run = false;
	vid_convert_request = false;
	vid_play_request = false;

	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(vid_decode_thread, time_out));
	Util_log_save(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(vid_convert_thread, time_out));
	threadFree(vid_decode_thread);
	threadFree(vid_convert_thread);

	for(int i = 0; i < 4; i++)
		Draw_c2d_image_free(vid_image[i]);
	
	Util_log_save(DEF_SAPP0_EXIT_STR, "Exited.");
}

void Sapp0_main(void)
{
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

	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, back_color);

		//video
		Draw_texture(vid_image[0].c2d, vid_x, vid_y, vid_tex_width[0] * vid_zoom, vid_tex_height[0] * vid_zoom);
		if(vid_width > 1024)
			Draw_texture(vid_image[1].c2d, (vid_x + vid_tex_width[0] * vid_zoom), vid_y, vid_tex_width[1] * vid_zoom, vid_tex_height[1] * vid_zoom);
		if(vid_height > 1024)
			Draw_texture(vid_image[2].c2d, vid_x, (vid_y + vid_tex_width[0] * vid_zoom), vid_tex_width[2] * vid_zoom, vid_tex_height[2] * vid_zoom);
		if(vid_width > 1024 && vid_height > 1024)
			Draw_texture(vid_image[3].c2d, (vid_x + vid_tex_width[0] * vid_zoom), (vid_y + vid_tex_height[0] * vid_zoom), vid_tex_width[3] * vid_zoom, vid_tex_height[3] * vid_zoom);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();

		Draw_screen_ready(1, back_color);

		Draw(DEF_SAPP0_VER, 0, 0, 0.4, 0.4, DEF_DRAW_GREEN);

		//codec info
		Draw(vid_video_format, 0, 10, 0.5, 0.5, color);
		Draw(vid_audio_format, 0, 20, 0.5, 0.5, color);
		Draw(std::to_string(vid_width) + "x" + std::to_string(vid_height) + "@" + std::to_string(vid_framerate).substr(0, 5) + "fps", 0, 30, 0.5, 0.5, color);

		//video
		Draw_texture(vid_image[0].c2d, vid_x - 40, vid_y - 240, vid_tex_width[0] * vid_zoom, vid_tex_height[0] * vid_zoom);
		if(vid_width > 1024)
			Draw_texture(vid_image[1].c2d, (vid_x + vid_tex_width[0] * vid_zoom) - 40, vid_y - 240, vid_tex_width[1] * vid_zoom, vid_tex_height[1] * vid_zoom);
		if(vid_height > 1024)
			Draw_texture(vid_image[2].c2d, vid_x - 40, (vid_y + vid_tex_width[0] * vid_zoom) - 240, vid_tex_width[2] * vid_zoom, vid_tex_height[2] * vid_zoom);
		if(vid_width > 1024 && vid_height > 1024)
			Draw_texture(vid_image[3].c2d, (vid_x + vid_tex_width[0] * vid_zoom) - 40, (vid_y + vid_tex_height[0] * vid_zoom) - 240, vid_tex_width[3] * vid_zoom, vid_tex_height[3] * vid_zoom);

		//controls
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 165, 165, 145, 10);
		Draw(vid_msg[2], 167.5, 165, 0.4, 0.4, color);

		//texture filter
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 10, 180, 145, 10);
		Draw(vid_msg[vid_linear_filter], 12.5, 180, 0.4, 0.4, color);

		//allow skip frames
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, 165, 180, 145, 10);
		Draw(vid_msg[4 + vid_allow_skip_frames], 167.5, 180, 0.4, 0.4, color);

		//time bar
		Draw(Util_convert_seconds_to_time(vid_current_pos / 1000) + "/" + Util_convert_seconds_to_time(vid_duration), 110, 210, 0.5, 0.5, color);
		Draw_texture(var_square_image[0], DEF_DRAW_GREEN, 5, 195, 310, 10);
		if(vid_duration != 0)
			Draw_texture(var_square_image[0], 0xFF800080, 5, 195, 310 * ((vid_current_pos / 1000) / vid_duration), 10);

		if(vid_detail_mode)
		{
			//decoding detail
			for(int i = 0; i < 319; i++)
			{
				Draw_line(i, 120 - vid_time[0][i], 0xFF0000FF, i + 1, 120 - vid_time[0][i + 1], 0xFF0000FF, 1);//video decode time graph
				Draw_line(i, 120 - vid_time[1][i], 0xFF0080FF, i + 1, 120 - vid_time[1][i + 1], 0xFF0080FF, 1);//audio decode time graph
				Draw_line(i, 120 - vid_time[2][i], 0xFF00FF00, i + 1, 120 - vid_time[2][i + 1], 0xFF00FF00, 1);//color convert time graph
				Draw_line(i, 120 - (vid_time[2][i] + vid_time[3][i]), 0xFFFF0000, i + 1, 120 - (vid_time[2][i + 1] + vid_time[3][i + 1]), 0xFFFF0000, 1);//data copy time graph
			}

			Draw_line(0, 120, color, 320, 120, color, 2);
			Draw_line(0, 120 - vid_frametime, 0xFFFFFF00, 320, 120 - vid_frametime, 0xFFFFFF00, 2);
			Draw("Deadline : " + std::to_string(vid_frametime).substr(0, 5) + "ms", 0, 120, 0.4, 0.4, 0xFFFFFF00);
			Draw("Video decode : " + std::to_string(vid_time[0][319]).substr(0, 5) + "ms", 160, 120, 0.4, 0.4, 0xFF0000FF);
			Draw("Audio decode : " + std::to_string(vid_time[1][319]).substr(0, 5) + "ms", 0, 130, 0.4, 0.4, 0xFF0080FF);
			Draw("Color convert : " + std::to_string(vid_time[2][319]).substr(0, 5) + "ms", 160, 130, 0.4, 0.4, 0xFF00FF00);
			Draw("Data copy : " + std::to_string(vid_time[3][319]).substr(0, 5) + "ms", 0, 140, 0.4, 0.4, 0xFFFF0000);
			Draw("Thread 0 : " + std::to_string(vid_time[4][319]).substr(0, 6) + "ms", 0, 150, 0.5, 0.5, color);
			Draw("Thread 1 : " + std::to_string(vid_time[5][319]).substr(0, 6) + "ms", 160, 150, 0.5, 0.5, color);
			Draw("Zoom : x" + std::to_string(vid_zoom).substr(0, 5) + " X : " + std::to_string((int)vid_x) + " Y : " + std::to_string((int)vid_y), 0, 160, 0.5, 0.5, color);
		}

		if(vid_show_controls)
		{
			Draw_texture(var_square_image[0], DEF_DRAW_AQUA, 80, 40, 160, 130);
			Draw(vid_msg[3], 82.5, 40, 0.5, 0.5, DEF_DRAW_BLACK);
		}

		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_bot_ui();
		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();

	if(Util_err_query_error_show_flag())
		Util_err_main(key);
	else if(Util_expl_query_show_flag())
		Util_expl_main(key);
	else
	{
		if (key.p_start || (key.p_touch && key.touch_x >= 110 && key.touch_x <= 230 && key.touch_y >= 220 && key.touch_y <= 240))
			Sapp0_suspend();
		else if(key.p_a)
		{
			if(vid_play_request)
				vid_pause_request = !vid_pause_request;
			else
				vid_play_request = true;

			var_need_reflesh = true;
		}
		else if(key.p_b)
		{
			vid_play_request = false;
			var_need_reflesh = true;
		}
		else if(key.p_x)
		{
			Util_expl_set_show_flag(true);
			Util_expl_set_callback(Sapp0_callback);
			Util_expl_set_cancel_callback(Sapp0_cancel);
		}
		else if(key.p_y)
		{
			vid_detail_mode = !vid_detail_mode;
			var_need_reflesh = true;
		}
		else if(key.p_touch && key.touch_x >= 5 && key.touch_x <= 314 && key.touch_y >= 195 && key.touch_y <= 204)
		{
			vid_seek_pos = (vid_duration * 1000) * (((double)key.touch_x - 5) / 310);
			vid_seek_request = true;
			var_need_reflesh = true;
		}
		else if(key.p_touch && key.touch_x >= 165 && key.touch_x <= 309 && key.touch_y >= 165 && key.touch_y <= 174)
		{
			vid_show_controls = !vid_show_controls;
			var_need_reflesh = true;
		}
		else if(key.p_touch && key.touch_x >= 10 && key.touch_x <= 154 && key.touch_y >= 180 && key.touch_y <= 189)
		{
			vid_linear_filter = !vid_linear_filter;
			for(int i = 0; i < 4; i++)
				Draw_c2d_image_set_filter(&vid_image[i], vid_linear_filter);

			var_need_reflesh = true;
		}
		else if(key.p_touch && key.touch_x >= 165 && key.touch_x <= 309 && key.touch_y >= 180 && key.touch_y <= 189)
		{
			vid_allow_skip_frames = !vid_allow_skip_frames;
			var_need_reflesh = true;
		}
		else if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		
		if(key.p_c_down || key.p_c_up || key.p_c_right || key.p_c_left || key.h_c_down || key.h_c_up || key.h_c_right || key.h_c_left
		|| key.p_d_down || key.p_d_up || key.p_d_right || key.p_d_left || key.h_d_down || key.h_d_up || key.h_d_right || key.h_d_left)
		{
			if(key.p_c_down || key.p_d_down)
				vid_y -= 1 * var_scroll_speed * key.count;
			else if(key.h_c_down || key.h_d_down)
			{
				if(vid_cd_count > 600)
					vid_y -= 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_y -= 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_y -= 5 * var_scroll_speed * key.count;
			}

			if(key.p_c_up || key.p_d_up)
				vid_y += 1 * var_scroll_speed * key.count;
			else if(key.h_c_up || key.h_d_up)
			{
				if(vid_cd_count > 600)
					vid_y += 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_y += 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_y += 5 * var_scroll_speed * key.count;
			}

			if(key.p_c_right || key.p_d_right)
				vid_x -= 1 * var_scroll_speed * key.count;
			else if(key.h_c_right || key.h_d_right)
			{
				if(vid_cd_count > 600)
					vid_x -= 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_x -= 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_x -= 5 * var_scroll_speed * key.count;
			}

			if(key.p_c_left || key.p_d_left)
				vid_x += 1 * var_scroll_speed * key.count;
			else if(key.h_c_left || key.h_d_left)
			{
				if(vid_cd_count > 600)
					vid_x += 10 * var_scroll_speed * key.count;
				else if(vid_cd_count > 240)
					vid_x += 7.5 * var_scroll_speed * key.count;
				else if(vid_cd_count > 5)
					vid_x += 5 * var_scroll_speed * key.count;
			}

			if(vid_x > 400)
				vid_x = 400;
			else if(vid_x < -vid_width * vid_zoom)
				vid_x = -vid_width * vid_zoom;

			if(vid_y > 480)
				vid_y = 480;
			else if(vid_y < -vid_height * vid_zoom)
				vid_y = -vid_height * vid_zoom;

			vid_cd_count++;
			var_need_reflesh = true;
		}
		else
			vid_cd_count = 0;

		if(key.p_l || key.p_r || key.h_l || key.h_r)
		{
			if(key.p_l)
				vid_zoom -= 0.005 * var_scroll_speed * key.count;
			else if(key.h_l)
			{
				if(vid_lr_count > 360)
					vid_zoom -= 0.05 * var_scroll_speed * key.count;
				else if(vid_lr_count > 120)
					vid_zoom -= 0.01 * var_scroll_speed * key.count;
				else if(vid_lr_count > 5)
					vid_zoom -= 0.005 * var_scroll_speed * key.count;
			}

			if(key.p_r)
				vid_zoom += 0.005 * var_scroll_speed * key.count;
			else if(key.h_r)
			{
				if(vid_lr_count > 360)
					vid_zoom += 0.05 * var_scroll_speed * key.count;
				else if(vid_lr_count > 120)
					vid_zoom += 0.01 * var_scroll_speed * key.count;
				else if(vid_lr_count > 5)
					vid_zoom += 0.005 * var_scroll_speed * key.count;
			}

			if(vid_zoom < 0.05)
				vid_zoom = 0.05;
			else if(vid_zoom > 10)
				vid_zoom = 10;

			vid_lr_count++;
			var_need_reflesh = true;
		}
		else
			vid_lr_count = 0;
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
}
