#pragma once
#include <utility>
#include <deque>
#include "types.hpp"

class VerticalScroller {
	// area of the scroller
	int x_l = 0;
	int x_r = 0;
	int y_l = 0;
	int y_r = 0;
	
	int offset = 0;
	
	int content_height;
	int last_touch_x = -1;
	int last_touch_y = -1;
	int first_touch_x = -1;
	int first_touch_y = -1;
	int touch_frames = 0;
	std::deque<int> touch_moves;
	float inertia = 0;
	float selected_darkness = 0;
	bool grabbed = false;
	bool scrolling = false;
public :
	VerticalScroller () = default;
	VerticalScroller (int x_l, int x_r, int y_l, int y_r) : VerticalScroller() {
		change_area(x_l, x_r, y_l, y_r);
	}
	
	void change_area(int x_l, int x_r, int y_l, int y_r) {
		this->x_l = x_l;
		this->x_r = x_r;
		this->y_l = y_l;
		this->y_r = y_r;
	}
	// if the touch is released on the content without scrolling, returns the relative coordinates of the releasing position from the top-left of the content
	// otherwise, returns {-1, -1}
	std::pair<int, int> update(Hid_info key, int content_y_len); // should only be called while the scroller is in the foreground
	void draw_slider_bar();
	void on_resume(); // should be called when the scroller is back in the foreground
	void reset(); // should be called when the scroll offset should be set to zero
	bool is_grabbed() { return grabbed; }
	bool is_scrolling() { return scrolling; }
	float selected_overlap_darkness() { return selected_darkness; }
	bool is_selecting() { return grabbed && !scrolling; }
	int get_offset() { return offset; }
};

