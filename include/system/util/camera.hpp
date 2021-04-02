#pragma once

Result_with_string Util_cam_init(std::string color_format);

Result_with_string Util_cam_take_a_picture(u8** raw_data, int* width, int* height, bool shutter_sound);

Result_with_string Util_cam_set_resolution(int width, int height);

void Util_cam_exit(void);
