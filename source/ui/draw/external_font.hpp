#pragma once

#define FONT_BLOCK_NUM 50
#define SYSTEM_FONT_NUM 4

void Extfont_init(void);
void Extfont_exit(void);

void Extfont_request_extfont_status(int i, bool loaded);
void Extfont_request_sysfont_status(int i, bool loaded);
bool Extfont_is_extfont_loaded(int block_id);
bool Extfont_is_sysfont_loaded(int id);

float Extfont_get_width_one(u32 c, float size);
float Extfont_get_width(const std::string &s, float size);

int Extfont_parse_utf8_str_to_u32(const char *in, u32 *out, int out_size);
void Extfont_sort_rtl(u32 *s, int n);

void Extfont_draw_extfonts(u32 *s, size_t len, float x, float y, float texture_size_x, float texture_size_y, int abgr8888, float* out_width);
