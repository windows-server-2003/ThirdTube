#pragma once

Result_with_string Util_mic_init(int buffer_size);

Result_with_string Util_mic_start_recording(int sample_rate);

void Util_mic_stop_recording(void);

bool Util_mic_is_recording(void);

Result_with_string Util_mic_get_audio_data(u8** raw_data, int* size);

void Util_mic_exit(void);
