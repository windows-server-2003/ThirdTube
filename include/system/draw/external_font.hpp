#pragma once

void Exfont_init(void);

void Exfont_exit(void);

std::string Exfont_query_external_font_name(int exfont_num);

bool Exfont_is_loaded_external_font(int exfont_num);

bool Exfont_is_loading_external_font(void);

bool Exfont_is_unloading_external_font(void);

bool Exfont_is_loaded_system_font(int system_font_num);

bool Exfont_is_loading_system_font(void);

bool Exfont_is_unloading_system_font(void);

void Exfont_set_external_font_request_state(int exfont_num, bool flag);

void Exfont_request_load_external_font(void);

void Exfont_request_unload_external_font(void);

void Exfont_set_system_font_request_state(int system_font_num, bool flag);

void Exfont_request_load_system_font(void);

void Exfont_request_unload_system_font(void);

std::string Exfont_text_sort(std::string sorce_part_string[], int max_loop);

void Exfont_text_parse(std::string sorce_string, std::string part_string[], int max_loop, int* out_element);

void Exfont_draw_external_fonts(std::string string, float texture_x, float texture_y, float texture_size_x, float texture_size_y, int abgr8888, float* out_width, float* out_height);

Result_with_string Exfont_load_exfont(int exfont_num);

void Exfont_unload_exfont(int exfont_num);
