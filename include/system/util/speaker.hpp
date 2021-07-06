#pragma once

void Util_speaker_init(int play_ch, int music_ch, int sample_rate);

Result_with_string Util_speaker_add_buffer(int play_ch, int music_ch, u8* buffer, int size, double pts);

double Util_speaker_get_current_timestamp(int play_ch, int sample_rate);

void Util_speaker_clear_buffer(int play_ch);

void Util_speaker_pause(int play_ch);

void Util_speaker_resume(int play_ch);

bool Util_speaker_is_paused(int play_ch);

bool Util_speaker_is_playing(int play_ch);

void Util_speaker_exit(int play_ch);
