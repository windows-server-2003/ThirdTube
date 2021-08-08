#pragma once
#include <utility>
#include <vector>
#include <deque>
#include "view.hpp"

class ScrollView : public View {
	int offset = 0;
	
	int last_touch_x = -1;
	int last_touch_y = -1;
	int first_touch_x = -1;
	int first_touch_y = -1;
	int content_height = 0;
	
	int touch_frames = 0;
	std::deque<int> touch_moves;
	float inertia = 0;
	float selected_darkness = 0;
	bool grabbed = false;
	bool scrolling = false;
public :
	std::vector<View *> views;
	
	using View::View;
	
	// direct access to `views` is also allowed
	// this is just for method chaining mainly used immediately after the construction of the view
	ScrollView *set_views(const std::vector<View *> &views) {
		this->views = views;
		return this;
	}
	
	
	void draw() const override {
		double y_offset = y0 - offset;
		for (auto view : views) {
			view->draw(x0, y_offset);
			y_offset += view->y1 - view->y0;
		}
		draw_slider_bar();
	}
	void update(Hid_info key) override {
		update_scroller(key);
		double y_offset = y0 - offset;
		for (auto view : views) {
			view->update(key, x0, y_offset);
			y_offset += view->y1 - view->y0;
		}
	}
	
	// if the touch is released on the content without scrolling, returns the relative coordinates of the releasing position from the top-left of the content
	// otherwise, returns {-1, -1}
	void update_scroller(Hid_info key); // should only be called while the scroller is in the foreground
	void draw_slider_bar() const;
	void on_resume(); // should be called when the scroller is back in the foreground
	void reset(); // should be called when the scroll offset should be set to zero
	bool is_grabbed() const { return grabbed; }
	bool is_scrolling() const { return scrolling; }
	float selected_overlap_darkness() const { return selected_darkness; }
	bool is_selecting() const { return grabbed && !scrolling; }
	int get_offset() const { return offset; }
};

