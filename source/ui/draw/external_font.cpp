#include "headers.hpp"
#include <numeric>

#define DEF_EXTFONT_INIT_STR (std::string)"Extfont/Init"
#define DEF_EXTFONT_EXIT_STR (std::string)"Extfont/Exit"
#define DEF_EXTFONT_LOAD_FONT_THREAD_STR (std::string)"Extfont/Load font thread"

/*
	   0 ~   95  (128) Basic latin
	  96 ~  223   (96) Latin 1 supplement
	 224 ~  319   (96) Ipa extensions
	 320 ~  399   (80) Spacing modifier letters
	 400 ~  511  (112) Combining diacritical marks
	 512 ~  646  (135) Greek and coptic
	 647 ~  902  (256) Cyrillic
	 903 ~  950   (48) Cyrillic supplement
	 951 ~ 1041   (89) Armenian
	1042 ~ 1129   (88) Hebrew
	1130 ~ 1384  (255) Arabic
	1385 ~ 1512  (128) Devanagari
	1513 ~ 1591   (79) Gurmukhi
	1592 ~ 1663   (72) Tamil
	1664 ~ 1759   (96) Telugu
	1760 ~ 1847   (88) Kannada
	1848 ~ 1937   (90) Sinhala
	1938 ~ 2024   (87) Thai
	2025 ~ 2091   (67) Lao
	2092 ~ 2302  (211) Tibetan
	2303 ~ 2390   (88) Georgian
	2391 ~ 3031  (640) Unified canadian aboriginal syllabics
	3032 ~ 3158  (128) Phonetic extensions
	3159 ~ 3221   (63) Combining diacritical marks supplement
	3222 ~ 3454  (233) Greek extended
	3455 ~ 3525  (71) General punctuation
	3566 ~ 3607   (42) Superscripts and subscripts
	3608 ~ 3640   (33) Combining diacritical marks for symbols
	3641 ~ 3752  (112) Arrows
	3753 ~ 4008  (256) Mathematical operators
	4009 ~ 4264  (256) Miscellaneous technical
	4265 ~ 4275   (11) Optical character recognition
	4276 ~ 4403  (128) Box drawing
	4404 ~ 4435   (32) Block elements
	4436 ~ 4531   (96) Geometric shapes
	4532 ~ 4787  (256) Miscellaneous_symbols
	4788 ~ 4979  (192) Dingbats
	4980 ~ 5043   (64) Cjk symbol and punctuation
	5044 ~ 5136   (93) Hiragana
	5137 ~ 5232   (96) Katakana
	5327 ~ 6491 (1165) Yi syllables
	6492 ~ 6546   (55) Yi radicals
	6547 ~ 6578   (32) Cjk compatibility forms
	6579 ~ 6803  (225) Halfwidth and fullwidth forms
	6804 ~ 7571  (768) Miscellaneous symbols and pictographs
*/


static bool inline is_top_byte(u32 c) { return (c & 0xC0) != 0x80; }
/*
	Characters that are not provided in the font but not printed as '?' either, thus "ignored"
	E2 80 8? (U+200?) : Various Whitespaces(EN QUAD, EM QUAD, etc...)
	EF B8 8? (U+FE0?) : Variation Selectors
*/
static bool inline is_ignored_character(u32 c) { return (c >> 4) == 0xE2808 || (c >> 4) == 0xEFB88; }

// For performance reasons, it handles multibyte characters by groups of 64 consecutive characters
// For multibyte characters the last byte always begins with 0b10, so one group corresponds to one combination of the upper byte(s)
// There are currently 144 groups
struct FontTable {
	std::string font_block_names[FONT_BLOCK_NUM];
	int font_block_char_num[FONT_BLOCK_NUM] = {
	 128,  96,  96,  80, 112, 135, 256,  48,  89,  88,  255, 128, 79,  72,  96,  88,
	  90,  87,  67, 211,  88, 640, 128,  63, 233,  71,  42,  33, 112, 256, 256,  11,
	 160, 128,  32,  96, 256, 192, 128, 252,  64,  93,  96, 256, 1165, 55,  32, 225,
	 768,  80,
	};
	int font_block_start_index[FONT_BLOCK_NUM + 1];
	static constexpr u32 ONE_BYTE_NUM = 0x80;
	// The range of the non-lowest bytes for each character length(not all values in the range are used)
	static constexpr u32 TWO_BYTE_LOW  = 0xC2;
	static constexpr u32 TWO_BYTE_HIGH = 0xDB;
	static constexpr u32 THREE_BYTE_LOW  = 0xE0A4;
	static constexpr u32 THREE_BYTE_HIGH = 0xEFBF;
	static constexpr u32 FOUR_BYTE_LOW  = 0xF09F8C;
	static constexpr u32 FOUR_BYTE_HIGH = 0xF09F99;
	static constexpr u32 NUM_GROUPS = (TWO_BYTE_HIGH - TWO_BYTE_LOW + 1) +
		(THREE_BYTE_HIGH - THREE_BYTE_LOW + 1) + (FOUR_BYTE_HIGH - FOUR_BYTE_LOW + 1);
	static constexpr u32 MAX_FONT_CHARS = 144 * 64 + 0x80;
	
	static constexpr u32 RLM = 0xE2808F; // Right-to-left mark
	// the group index for each used byte(((u8) -1) for unused groups)
	u8 group_index_2[TWO_BYTE_HIGH - TWO_BYTE_LOW + 1];
	u8 group_index_3[THREE_BYTE_HIGH - THREE_BYTE_LOW + 1];
	u8 group_index_4[FOUR_BYTE_HIGH - FOUR_BYTE_LOW + 1];
	
	static constexpr int MAX_RTL_CHARS = 256;
	static constexpr u32 RTL_CHAR_LOW = 0xD6BE;
	static constexpr u32 RTL_CHAR_HIGH = 0xDBBF;
	int rtl_char_num;
	u32 rtl_characters[MAX_RTL_CHARS];
	
	u32 *font_samples;
	C2D_Image font_images[MAX_FONT_CHARS];
	static constexpr float INTERVAL_OFFSET = 0.5;
	
	// returns (u32) -1 if it's out of the loaded font
	// in case of an invalid utf-8 char, it may return wrong answer but does not cause out-out-bounds access
	u32 char_to_font_index(u32 c) {
		if (c < 0x80) return c;
		
		u32 c2 = c >> 8;
		u8 group_index;
		if (c2 <= TWO_BYTE_HIGH) {
			if (c2 < TWO_BYTE_LOW) return -1;
			group_index = group_index_2[c2 - TWO_BYTE_LOW];
		} else if (c2 <= THREE_BYTE_HIGH) {
			if (c2 < THREE_BYTE_LOW) return -1;
			group_index = group_index_3[c2 - THREE_BYTE_LOW];
		} else if (c2 <= FOUR_BYTE_HIGH) {
			if (c2 < FOUR_BYTE_LOW) return -1;
			group_index = group_index_4[c2 - FOUR_BYTE_LOW];
		} else return -1;
		if (group_index == (u8) -1) return -1;
		return 0x80 + ((u32) group_index << 6) + (c & 63);
	}
	
	void load_initial() {
		u8* fs_buffer = (u8 *) malloc(0x8000);
		u32 size_read = 0;
		Result_with_string result;

		// load font names
		memset(fs_buffer, 0x0, 0x8000);
		result = Path("romfs:/gfx/msg/font_name.txt").read_file(fs_buffer, 0x2000, size_read);
		if (result.code != 0) logger.error(DEF_EXTFONT_INIT_STR, "Load font_name.txt: " + result.string + result.error_description, result.code);
		
		result = Util_parse_file((char *) fs_buffer, FONT_BLOCK_NUM, font_block_names);
		if (result.code != 0) logger.error(DEF_EXTFONT_INIT_STR, "Util_parse_file()..." + result.string + result.error_description, result.code);

		// load characters included in the font
		int characters = 0;
		font_samples = (u32 *) malloc(MAX_FONT_CHARS * sizeof(u32));
		*font_samples = 0; // NULL character is not included in font_samples.txt, so add here
		memset(fs_buffer, 0x0, 0x8000);
		result = Path("romfs:/gfx/font/sample/font_samples.txt").read_file(fs_buffer, 0x8000, size_read);
		if (result.code == 0) characters = parse_utf8_str_to_u32((const char *) fs_buffer, font_samples + 1, MAX_FONT_CHARS) + 1; // because of the NULL character
		else logger.error(DEF_EXTFONT_INIT_STR, "Load font_samples.txt: " + result.string + result.error_description, result.code);
		// init group_index_*
		std::fill(std::begin(group_index_2), std::end(group_index_2), (u8) -1);
		std::fill(std::begin(group_index_3), std::end(group_index_3), (u8) -1);
		std::fill(std::begin(group_index_4), std::end(group_index_4), (u8) -1);
		for (int i = 0; i < characters; i++) {
			u32 c = font_samples[i];
			u32 c2 = c >> 8;
			if (!c2) continue;
			if (c2 < 0x100) {
				if (c2 < TWO_BYTE_LOW || c2 > TWO_BYTE_HIGH) logger.error(DEF_EXTFONT_INIT_STR, "Unknown character in sample: " + std::to_string(c));
				else group_index_2[c2 - TWO_BYTE_LOW] = 0; // flag indicating the group is used
			} else if (c2 < 0x10000) {
				if (c2 < THREE_BYTE_LOW || c2 > THREE_BYTE_HIGH) logger.error(DEF_EXTFONT_INIT_STR, "Unknown character in sample: " + std::to_string(c));
				else group_index_3[c2 - THREE_BYTE_LOW] = 0; // flag indicating the group is used
			} else {
				if (c2 < FOUR_BYTE_LOW || c2 > FOUR_BYTE_HIGH) logger.error(DEF_EXTFONT_INIT_STR, "Unknown character in sample: " + std::to_string(c));
				else group_index_4[c2 - FOUR_BYTE_LOW] = 0; // flag indicating the group is used
			}
		}
		int group_cnt = 0;
		for (auto &i : group_index_2) if (i != (u8) -1) i = group_cnt++;
		for (auto &i : group_index_3) if (i != (u8) -1) i = group_cnt++;
		for (auto &i : group_index_4) if (i != (u8) -1) i = group_cnt++;
		if (group_cnt >= 256) logger.error(DEF_EXTFONT_INIT_STR, "Too many character groups: " + std::to_string(group_cnt));
		// init font_block_start_index
		int acc_char_num = 0;
		for (int i = 0; i < FONT_BLOCK_NUM; i++) font_block_start_index[i] = char_to_font_index(font_samples[acc_char_num]), acc_char_num += font_block_char_num[i];
		font_block_start_index[FONT_BLOCK_NUM] = 0x80 * (group_cnt << 6); // the end of all the font blocks
		
		// load RTL(Right-to-left) character list
		memset(fs_buffer, 0x0, 0x8000);
		result = Path("romfs:/gfx/font/sample/font_right_to_left_samples.txt").read_file(fs_buffer, 0x8000, size_read);
		if (result.code == 0) rtl_char_num = parse_utf8_str_to_u32((const char *) fs_buffer, rtl_characters, MAX_RTL_CHARS);
		else logger.error(DEF_EXTFONT_INIT_STR, "Load font_right_to_left_samples.txt: " + result.string + result.error_description, result.code);
		for (int i = 0; i < rtl_char_num; i++) if (rtl_characters[i] < RTL_CHAR_LOW || rtl_characters[i] > RTL_CHAR_HIGH) 
			logger.error(DEF_EXTFONT_INIT_STR, "Unknown RTL char: " + std::to_string(rtl_characters[i]));
		
		free(fs_buffer);
		
		// clear font_images
		memset(font_images, 0, sizeof(font_images));
	}
	void deinit() {
		free(font_samples);
		font_samples = NULL;
	}

	void unload_font_block(int block_id) {
		int start = font_block_start_index[block_id];
		int end = font_block_start_index[block_id + 1];
		Draw_free_texture(5 + block_id);
		for (int j = start; j < end; j++) font_images[j].tex = NULL, font_images[j].subtex = NULL;
	}

	Result_with_string load_font_block(int block_id) {
		int start_index = font_block_start_index[block_id];
		int end_index = font_block_start_index[block_id + 1];
		int char_num = font_block_char_num[block_id];
		Result_with_string result = Draw_load_texture("romfs:/gfx/font/" + font_block_names[block_id] + "_font.t3x", block_id + 5, font_images, start_index, char_num);
		// move them to the correct position(due to gaps, the correct position may be later than where it is now)
		int base = std::accumulate(font_block_char_num, font_block_char_num + block_id, 0);
		for (int i = char_num; i--; ) {
			int to = char_to_font_index(font_samples[base + i]);
			if (to > start_index + i) {
				font_images[to] = font_images[start_index + i];
				font_images[start_index + i].tex = NULL;
				font_images[start_index + i].subtex = NULL;
			}
			if (to < start_index + i) logger.error("to < i (" + std::to_string(to) + " < " + std::to_string(start_index + i) + ")");
		}

		if (result.code == 0) {
			for (int i = 0; i < char_num; i++) {
				int index = char_to_font_index(font_samples[base + i]);
				C3D_TexSetFilter(font_images[index].tex, GPU_LINEAR, GPU_LINEAR);
				C3D_TexSetWrap(font_images[index].tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			}
		} else unload_font_block(block_id);
		return result;
	}
	// returns the number of characters written to *out
	int parse_utf8_str_to_u32(const char *in, u32 *out, int out_size) {
		while (!is_top_byte(*in)) in++; // NULL is also a top byte
		int i;
		for (i = 0; i < out_size && *in; ) {
			u32 cur_c = 0;
			int j;
			for (j = 1; j <= 4; j++) {
				cur_c = (cur_c << 8) + in[j - 1];
				if (is_top_byte(in[j])) {
					out[i++] = cur_c;
					in += j;
					break;
				}
			}
			if (j == 5) while (!is_top_byte(*++in)); // not found -> skip until next top byte(including NULL)
		}
		return i;
	}
	void sort_rtl(u32 *s, int n) {
		int reverse_start = -1;
		for (int i = 0; i < n; i++) {
			bool is_right_to_left = false;
			
			if (s[i] == RLM) is_right_to_left = true;
			else if (s[i] > RTL_CHAR_LOW && s[i] < RTL_CHAR_HIGH)
				is_right_to_left = (std::find(rtl_characters, rtl_characters + rtl_char_num, s[i]) != rtl_characters + rtl_char_num);
			
			if (is_right_to_left && reverse_start == -1) reverse_start = i; // start of reverse characters
			if (!is_right_to_left && reverse_start != -1) { // end of reverse characters
				std::reverse(s + reverse_start, s + i);
				reverse_start = -1;
			}
		}
		if (reverse_start != -1) std::reverse(s + reverse_start, s + n);
	}

	bool is_font_available(int index) { return font_images[index].subtex; }
	float get_width_by_index(int index) { return font_images[index].subtex->width; }
	float get_width_one(u32 c, float size) {
		u32 index = char_to_font_index(c);
		// out of bounds or font not loaded/unused character -> the width of <?>
		if (index != (u32) -1) my_assert(index < MAX_FONT_CHARS);
		if (index == (u32) -1 || !is_font_available(index)) 
			return is_ignored_character(c) || !is_font_available(0) ? 0 : (get_width_by_index(0) + INTERVAL_OFFSET) * size;
		return (get_width_by_index(index) + INTERVAL_OFFSET) * size;
	}
	float get_width(const std::string &s, float size) {
		u32 *buf = (u32 *) malloc(sizeof(u32) * s.size());
		int char_num = parse_utf8_str_to_u32(s.c_str(), buf, s.size());
		float res = 0;
		for (int i = 0; i < char_num; i++) res += get_width_one(buf[i], size);
		free(buf);
		return res;
	}
	
	void draw(u32 *s, size_t len, float x, float y, float texture_size_x, float texture_size_y, int abgr8888, float *out_width) {
		float x_offset = 0.0;
		
		for (size_t i = 0; i < len; i++) {
			if (!s[i]) break;
			u32 index = char_to_font_index(s[i]);
			if (index == (u32) -1) {
				if (is_ignored_character(s[i])) continue;
				index = 0; // index 0: <?>
			}
			if (!font_images[index].subtex) continue;
			float x_size = (get_width_by_index(index) + INTERVAL_OFFSET) * texture_size_x;
			Draw_texture(font_images[index], abgr8888, x + x_offset, y, x_size, 20.0 * texture_size_y);
			x_offset += x_size;
		}
		
		*out_width = x_offset;
	}

};
static FontTable font_table;

namespace ExtFont {
	// zero-cleared
	bool font_block_loaded[FONT_BLOCK_NUM];
	volatile bool font_block_requested_state[FONT_BLOCK_NUM];
	bool system_font_loaded[SYSTEM_FONT_NUM];
	volatile bool system_font_requested_state[SYSTEM_FONT_NUM];
	
	Thread loader_thread;
	volatile bool loader_thread_should_be_running = false;
};
using namespace ExtFont;

static void loader_thread_func(void *) {
	while (loader_thread_should_be_running) {
		// external font
		for (int i = 0; i < FONT_BLOCK_NUM; i++) if (font_block_loaded[i] != font_block_requested_state[i]) {
			if (font_block_requested_state[i]) { // load
				auto result = font_table.load_font_block(i);
				if (result.code != 0) logger.error(DEF_EXTFONT_LOAD_FONT_THREAD_STR, "Failed to load ext font #" + std::to_string(i) + " : " +
					result.string + result.error_description, result.code);
				else font_block_loaded[i] = true;
			} else { // unload
				font_block_loaded[i] = false;
				font_table.unload_font_block(i);
			}
		}
		// system font
		for (int i = 0; i < SYSTEM_FONT_NUM; i++) if (system_font_loaded[i] != system_font_requested_state[i]) {
			if (system_font_requested_state[i]) { // load
				if (i == var_system_region) system_font_loaded[i] = true; // already loaded
				else {
					auto result = Draw_load_system_font(i);
					if (result.code != 0) logger.error(DEF_EXTFONT_LOAD_FONT_THREAD_STR, "Failed to load system font #" + std::to_string(i) + " : " +
						result.string + result.error_description, result.code);
					else system_font_loaded[i] = true;
				}
			} else { // unload
				if (i != var_system_region) {
					system_font_loaded[i] = false;
					Draw_free_system_font(i);
				}
			}
		}
		usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
	}
}

void Extfont_init(void) {
	logger.info(DEF_EXTFONT_INIT_STR, "Initializing...");
	
	font_table.load_initial();
	loader_thread_should_be_running = true;
	loader_thread = threadCreate(loader_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	
	logger.info(DEF_EXTFONT_INIT_STR, "Initialized.");
}

void Extfont_exit(void) {
	logger.info(DEF_EXTFONT_EXIT_STR, "Exiting...");

	loader_thread_should_be_running = false;
	logger.info(DEF_EXTFONT_EXIT_STR, "threadJoin()...", threadJoin(loader_thread, 10000000000));
	threadFree(loader_thread);

	font_table.deinit();

	logger.info(DEF_EXTFONT_EXIT_STR, "Exited.");
}

void Extfont_request_extfont_status(int i, bool loaded) { font_block_requested_state[i] = loaded; }
void Extfont_request_sysfont_status(int i, bool loaded) { system_font_requested_state[i] = loaded; }
bool Extfont_is_extfont_loaded(int block_id) {
	my_assert(block_id >= 0 && block_id < FONT_BLOCK_NUM);
	return font_block_loaded[block_id];
}
bool Extfont_is_sysfont_loaded(int id) {
	my_assert(id >= 0 && id < SYSTEM_FONT_NUM);
	return system_font_loaded[id];
}

float Extfont_get_width_one(u32 c, float size) { return font_table.get_width_one(c, size); }
float Extfont_get_width(const std::string &s, float size) { return font_table.get_width(s, size); }

int Extfont_parse_utf8_str_to_u32(const char *in, u32 *out, int out_size) { return font_table.parse_utf8_str_to_u32(in, out, out_size); }
void Extfont_sort_rtl(u32 *s, int n) { return font_table.sort_rtl(s, n); }

void Extfont_draw_extfonts(u32 *s, size_t len, float x, float y, float texture_size_x, float texture_size_y, int abgr8888, float* out_width) {
	font_table.draw(s, len, x, y, texture_size_x, texture_size_y, abgr8888, out_width);
}
