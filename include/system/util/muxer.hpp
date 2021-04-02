#pragma once

Result_with_string Util_muxer_mux(std::string file_name, int session);

Result_with_string Util_muxer_open_audio_file(std::string file_path, int session);

Result_with_string Util_muxer_open_video_file(std::string file_path, int session);

void Util_muxer_close_audio_file(int session);

void Util_muxer_close_video_file(int session);
