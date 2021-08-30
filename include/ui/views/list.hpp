#pragma once
#include "view.hpp"
#include <vector>

struct ListView : public FixedWidthView {
public :
	using FixedWidthView::FixedWidthView;
	virtual ~ListView () {}
	
	std::vector<View *> views;
	
	virtual void recursive_delete_subviews() override {
		for (auto view : views) {
			view->recursive_delete_subviews();
			delete view;
		}
		views.clear();
	}
	
	// direct access to `views` is also allowed
	// this is just for method chaining mainly used immediately after the construction of the view
	ListView *set_views(const std::vector<View *> &views) {
		this->views = views;
		return this;
	}
	
	float get_height() const override {
		float res = 0;
		for (auto view : views) res += view->get_height();
		return res;
	}
	void draw() const override {
		double y_offset = y0;
		for (auto view : views) {
			double y_bottom = y_offset + view->get_height();
			if (y_bottom >= 0 && y_offset < 240) view->draw(x0, y_offset);
			y_offset = y_bottom;
		}
	}
	void update(Hid_info key) override {
		double y_offset = y0;
		for (auto view : views) {
			view->update(key, x0, y_offset);
			y_offset += view->get_height();
		}
	}
	std::pair<int, int> get_displayed_range(int offset) {
		int cur_y = offset;
		int l = views.size(), r = 0;
		for (int i = 0; i < (int) views.size(); i++) {
			if (cur_y < 240) r = i;
			cur_y += views[i]->get_height();
			if (cur_y >= 0) l = std::min(l, i);
		}
		if (l > r) return {0, -1};
		return {l, r};
	}
};

