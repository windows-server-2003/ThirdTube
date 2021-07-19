#include "ui/scroller.hpp"
#include "variables.hpp"

std::pair<int, int> VerticalScroller::update(Hid_info key, int content_y_len) {
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
	if (offset < 0) offset = 0;
	if (offset > scroll_max) offset = scroll_max;
	
	if (grabbed && !scrolling) selected_darkness = std::min(1.0, selected_darkness + 0.10);
	else selected_darkness = std::max(0.0, selected_darkness - 0.10);
	if (key.touch_x == -1) selected_darkness = 0;
	
	std::pair<int, int> res = {-1, -1};
	if (!scrolling && grabbed && key.touch_x == -1 && last_touch_x != -1 && first_touch_x != -1) {
		if (last_touch_y >= y_l && last_touch_y < std::min(y_l + content_y_len, y_r)) {
			res = {last_touch_x - x_l, last_touch_y + offset - y_l};
		}
	}
	
	if (key.touch_y == -1) {
		scrolling = grabbed = false;
		touch_frames = 0;
	} else touch_frames++;
	last_touch_x = key.touch_x;
	last_touch_y = key.touch_y;
	return res;
}
void VerticalScroller::on_resume() {
	last_touch_x = last_touch_y = -1;
	first_touch_x = first_touch_y = -1;
	touch_frames = 0;
	selected_darkness = 0;
	scrolling = false;
	grabbed = false;
}
void VerticalScroller::reset() {
	on_resume();
	offset = 0;
}

