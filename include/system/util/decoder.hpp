#pragma once

Result_with_string Util_mvd_video_decoder_init(void);

struct NetworkStreamCacherData;
Result_with_string Util_decoder_open_network_stream(NetworkStreamCacherData *cacher, bool* has_audio, bool* has_video, int session);
Result_with_string Util_decoder_open_file(std::string file_path, bool* has_audio, bool* has_video, int session);

Result_with_string Util_audio_decoder_init(int session);

Result_with_string Util_video_decoder_init(int low_resolution, int session);

void Util_audio_decoder_get_info(int* bitrate, int* sample_rate, int* ch, std::string* format_name, double* duration, int session);

void Util_video_decoder_get_info(int* width, int* height, double* framerate, std::string* format_name, double* duration, int session);

Result_with_string Util_decoder_read_packet(std::string* type, int session);

Result_with_string Util_decoder_ready_audio_packet(int session);

Result_with_string Util_decoder_ready_video_packet(int session);

void Util_decoder_skip_audio_packet(int session);

void Util_decoder_skip_video_packet(int session);

Result_with_string Util_audio_decoder_decode(int* size, u8** raw_data, double* current_pos, int session);

Result_with_string Util_video_decoder_decode(int* width, int* height, bool* key_frame, double* current_pos, int session);

Result_with_string Util_mvd_video_decoder_decode(int* width, int* height, bool* key_frame, double* current_pos, int session);

Result_with_string Util_video_decoder_get_image(u8** raw_data, int width, int height, int session);

Result_with_string Util_mvd_video_decoder_get_image(u8** raw_data, int width, int height, int session);

Result_with_string Util_decoder_seek(u64 seek_pos, int flag, int session);

void Util_decoder_close_file(int session);

void Util_audio_decoder_exit(int session);

void Util_video_decoder_exit(int session);

void Util_mvd_video_decoder_exit(void);

Result_with_string Util_image_decoder_decode(std::string file_name, u8** raw_data, int* width, int* height);