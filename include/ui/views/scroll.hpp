#pragma once
#include <utility>
#include <vector>
#include <deque>
#include <functional>
#include "view.hpp"

class ScrollView : public FixedSizeView {
protected :
	int offset = 0;
	
	int last_touch_x = -1;
	int last_touch_y = -1;
	int first_touch_x = -1;
	int first_touch_y = -1;
	double content_height = 0;
	
	int touch_frames = 0;
	std::deque<int> touch_moves;
	float inertia = 0;
	float selected_darkness = 0;
	bool grabbed = false;
	bool scrolling = false;
public :
	std::vector<View *> views;
	double margin = 0.0;
	
	ScrollView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~ScrollView () {}
	
	using OnDrawnCallBackFuncType = std::function<void (const ScrollView &, int)>;
	std::vector<std::pair<int, OnDrawnCallBackFuncType> > on_child_drawn_callbacks;
	
	virtual void recursive_delete_subviews() override {
		for (auto view : views) {
			view->recursive_delete_subviews();
			delete view;
		}
		views.clear();
	}
	void reset_holding_status_() override {
		grabbed = false;
		scrolling = false;
		for (auto view : views) view->reset_holding_status();
	}
	
	// direct access to `views` is also allowed
	// this is just for method chaining mainly used immediately after the construction of the view
	ScrollView *set_views(const std::vector<View *> &views) {
		this->views = views;
		return this;
	}
	ScrollView *set_on_child_drawn(int index, OnDrawnCallBackFuncType func) {
		bool found = false;
		for (auto &i : on_child_drawn_callbacks) if (i.first == index) {
			found = true;
			i.second = func;
		}
		if (!found) on_child_drawn_callbacks.push_back({index, func});
		return this;
	}
	ScrollView *set_margin(double margin) {
		this->margin = margin;
		return this;
	}
	
	
	void draw_() const override {
		double y_offset = y0 - offset;
		for (int i = 0; i < (int) views.size(); i++) {
			auto &view = views[i];
			double cur_height = view->get_height();
			if (y_offset < y1 && y_offset + cur_height > 0) {
				view->draw(x0, y_offset);
				for (auto &callback : on_child_drawn_callbacks) if (callback.first == i) callback.second(*this, i);
			}
			y_offset += cur_height + margin;
		}
		draw_slider_bar();
	}
	void update_(Hid_info key) override {
		update_scroller(key);
		
		if (key.touch_x < x0 || key.touch_x >= x1 || key.touch_y < y0 || key.touch_y >= y1) {
			key.touch_x = -1;
			key.touch_y = -1;
			key.p_touch = false;
		}
		double y_offset = y0 - offset;
		for (auto view : views) {
			if (scrolling) view->on_scroll();
			view->update(key, x0, y_offset);
			y_offset += view->get_height() + margin;
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
	void set_offset(double offset) { this->offset = offset; }
};

