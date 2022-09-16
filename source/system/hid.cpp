#include "headers.hpp"

namespace Hid {
	u32 key_down;
	u32 key_held;
	touchPosition touch_pos;
	u32 prev_key_down;
	u32 prev_key_held;
	touchPosition prev_touch_pos;
	circlePosition circle_pos;
	int count = 0;
};
using namespace Hid;


void Util_hid_update_key_state() {
	prev_key_down = key_down;
	prev_key_held = key_held;
	prev_touch_pos = touch_pos;
	
	hidScanInput();
	hidTouchRead(&touch_pos);
	hidCircleRead(&circle_pos);
	key_held = hidKeysHeld();
	key_down = hidKeysDown();
	count++;
}


void Util_hid_query_key_state(Hid_info* out_key_state) {
	if (key_down | key_held) {
		if (var_afk_time > var_time_to_turn_off_lcd) key_down = 0; // the tap to awake the lcd should be ignored in most cases
		var_afk_time = 0;
	}
	
	out_key_state->p_a        = key_down & KEY_A;
	out_key_state->p_b        = key_down & KEY_B;
	out_key_state->p_x        = key_down & KEY_X;
	out_key_state->p_y        = key_down & KEY_Y;
	out_key_state->p_c_up     = key_down & KEY_CPAD_UP;
	out_key_state->p_c_down   = key_down & KEY_CPAD_DOWN;
	out_key_state->p_c_left   = key_down & KEY_CPAD_LEFT;
	out_key_state->p_c_right  = key_down & KEY_CPAD_RIGHT;
	out_key_state->p_d_up     = key_down & KEY_DUP;
	out_key_state->p_d_down   = key_down & KEY_DDOWN;
	out_key_state->p_d_left   = key_down & KEY_DLEFT;
	out_key_state->p_d_right  = key_down & KEY_DRIGHT;
	out_key_state->p_l        = key_down & KEY_L;
	out_key_state->p_r        = key_down & KEY_R;
	out_key_state->p_zl       = key_down & KEY_ZL;
	out_key_state->p_zr       = key_down & KEY_ZR;
	out_key_state->p_start    = key_down & KEY_START;
	out_key_state->p_select   = key_down & KEY_SELECT;
	out_key_state->p_cs_up    = key_down & KEY_CSTICK_UP;
	out_key_state->p_cs_down  = key_down & KEY_CSTICK_DOWN;
	out_key_state->p_cs_left  = key_down & KEY_CSTICK_LEFT;
	out_key_state->p_cs_right = key_down & KEY_CSTICK_RIGHT;
	out_key_state->p_touch    = key_down & KEY_TOUCH;
	out_key_state->h_a        = key_held & KEY_A;
	out_key_state->h_b        = key_held & KEY_B;
	out_key_state->h_x        = key_held & KEY_X;
	out_key_state->h_y        = key_held & KEY_Y;
	out_key_state->h_c_up     = key_held & KEY_CPAD_UP;
	out_key_state->h_c_down   = key_held & KEY_CPAD_DOWN;
	out_key_state->h_c_left   = key_held & KEY_CPAD_LEFT;
	out_key_state->h_c_right  = key_held & KEY_CPAD_RIGHT;
	out_key_state->h_d_up     = key_held & KEY_DUP;
	out_key_state->h_d_down   = key_held & KEY_DDOWN;
	out_key_state->h_d_left   = key_held & KEY_DLEFT;
	out_key_state->h_d_right  = key_held & KEY_DRIGHT;
	out_key_state->h_l        = key_held & KEY_L;
	out_key_state->h_r        = key_held & KEY_R;
	out_key_state->h_zl       = key_held & KEY_ZL;
	out_key_state->h_zr       = key_held & KEY_ZR;
	out_key_state->h_start    = key_held & KEY_START;
	out_key_state->h_select   = key_held & KEY_SELECT;
	out_key_state->h_cs_up    = key_held & KEY_CSTICK_UP;
	out_key_state->h_cs_down  = key_held & KEY_CSTICK_DOWN;
	out_key_state->h_cs_left  = key_held & KEY_CSTICK_LEFT;
	out_key_state->h_cs_right = key_held & KEY_CSTICK_RIGHT;
	out_key_state->h_touch    = key_held & KEY_TOUCH;
	out_key_state->cpad_x     = circle_pos.dx;
	out_key_state->cpad_y     = circle_pos.dy;
	out_key_state->touch_x_prev = ((prev_key_down | prev_key_held) & KEY_TOUCH) ? prev_touch_pos.px : -1;
	out_key_state->touch_y_prev = ((prev_key_down | prev_key_held) & KEY_TOUCH) ? prev_touch_pos.px : -1;
	out_key_state->touch_x      = ((key_down | key_held) & KEY_TOUCH) ? touch_pos.px : -1;
	out_key_state->touch_y      = ((key_down | key_held) & KEY_TOUCH) ? touch_pos.py : -1;
	out_key_state->count      = count;
}
