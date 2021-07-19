#include <deque>
#include <numeric>
#include "ui/scroller.hpp"
#include "variables.hpp"
#include "headers.hpp"

std::pair<int, int> VerticalScroller::update(Hid_info key, int content_y_len) {
	if (this->content_height != content_y_len) {
		this->content_height = content_y_len;
		var_need_reflesh = true;
	}
	int scroll_max = std::max(0, content_y_len - (y_r - y_l));
	if (key.p_touch) {
		first_touch_x = key.touch_x;
		first_touch_y = key.touch_y;
		if (x_l <= key.touch_x && key.touch_x < x_r && y_l <= key.touch_y && key.touch_y < std::min(y_l + content_y_len, y_r) &&
			var_afk_time <= var_time_to_turn_off_lcd) grabbed = true;
	} else if (grabbed && !scrolling && key.touch_y != -1 && std::abs(key.touch_y - first_touch_y) >= 5) {
		scrolling = true;
		offset += first_touch_y - key.touch_y;
	} else if (scrolling && key.touch_y != -1) {
		offset += last_touch_y - key.touch_y;
	}
	offset += inertia;
	if (inertia) var_need_reflesh = true;
	if (offset < 0) {
		offset = 0;
		inertia = 0;
	}
	if (offset > scroll_max) {
		offset = scroll_max;
		inertia = 0;
	}
	
	if (grabbed && !scrolling) selected_darkness = std::min(1.0, selected_darkness + 0.15);
	else selected_darkness = std::max(0.0, selected_darkness - 0.15);
	if (key.touch_x == -1) selected_darkness = 0;
	
	if (key.touch_x != -1) inertia = 0;
	else if (inertia > 0) inertia = std::max(0.0, inertia - 0.1);
	else inertia = std::min(0.0, inertia + 0.1);
	if (scrolling && key.touch_x == -1 && touch_frames >= 4) {
		int sample = std::min<int>(3, touch_moves.size());
		float amount = std::accumulate(touch_moves.end() - sample, touch_moves.end(), 0.0f) / sample;
		// Util_log_save("scroller", "inertia start : " + std::to_string(amount));
		if (std::fabs(amount) >= 8) inertia = -amount;
	}
	
	std::pair<int, int> res = {-1, -1};
	if (!scrolling && grabbed && key.touch_x == -1 && last_touch_x != -1 && first_touch_x != -1) {
		if (last_touch_y >= y_l && last_touch_y < std::min(y_l + content_y_len, y_r)) {
			res = {last_touch_x - x_l, last_touch_y + offset - y_l};
		}
	}
	
	if (key.touch_y != -1 && last_touch_y != -1) {
		touch_moves.push_back(key.touch_y - last_touch_y);
		if (touch_moves.size() > 10) touch_moves.pop_front();
	} else touch_moves.clear();
	if (key.touch_y == -1) {
		scrolling = grabbed = false;
		touch_frames = 0;
	} else touch_frames++;
	last_touch_x = key.touch_x;
	last_touch_y = key.touch_y;
	return res;
}
void VerticalScroller::draw_slider_bar() {
	float displayed_height = y_r - y_l;
	if (content_height > displayed_height) {
		float bar_len = displayed_height * displayed_height / content_height;
		float bar_pos = (float) offset / content_height * displayed_height;
		Draw_texture(var_square_image[0], DEF_DRAW_GRAY, x_r - 3, y_l + bar_pos, 3, bar_len);
	}
}
void VerticalScroller::on_resume() {
	last_touch_x = last_touch_y = -1;
	first_touch_x = first_touch_y = -1;
	touch_frames = 0;
	touch_moves.clear();
	selected_darkness = 0;
	scrolling = false;
	grabbed = false;
}
void VerticalScroller::reset() {
	on_resume();
	offset = 0;
}

