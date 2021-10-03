#include <deque>
#include <numeric>
#include "ui/views/scroll.hpp"
#include "ui/ui_common.hpp"
#include "variables.hpp"

void ScrollView::update_scroller(Hid_info key) {
	content_height = 0;
	for (auto view : views) content_height += view->get_height();
	content_height += std::max((int) views.size() - 1, 0) * margin;
	
	double scroll_max = std::max<double>(0, content_height - (y1 - y0));
	if (key.p_touch) {
		first_touch_x = key.touch_x;
		first_touch_y = key.touch_y;
		if (x0 <= key.touch_x && key.touch_x < x1 && y0 <= key.touch_y && key.touch_y < std::min(y0 + content_height, y1) &&
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
}
void ScrollView::draw_slider_bar() const {
	float displayed_height = y1 - y0;
	if (content_height > displayed_height) {
		float bar_len = displayed_height * displayed_height / content_height;
		float bar_pos = (float) offset / content_height * displayed_height;
		Draw_texture(var_square_image[0], DEF_DRAW_GRAY, x1 - 3, y0 + bar_pos, 3, bar_len);
	}
}
void ScrollView::on_resume() {
	last_touch_x = last_touch_y = -1;
	first_touch_x = first_touch_y = -1;
	touch_frames = 0;
	touch_moves.clear();
	selected_darkness = 0;
	scrolling = false;
	grabbed = false;
}
void ScrollView::reset() {
	on_resume();
	offset = 0;
}

