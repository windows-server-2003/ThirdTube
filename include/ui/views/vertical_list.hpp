#pragma once
#include "view.hpp"
#include <vector>

struct VerticalListView : public FixedWidthView {
public :
	VerticalListView (double x0, double y0, double width) : View(x0, y0), FixedWidthView(x0, y0, width) {}
	virtual ~VerticalListView () {}
	
	std::vector<View *> views;
	std::vector<int> draw_order;
	
	void reset_holding_status_() override {
		for (auto view : views) view->reset_holding_status();
	}
	virtual void recursive_delete_subviews() override {
		for (auto view : views) {
			view->recursive_delete_subviews();
			delete view;
		}
		views.clear();
	}
	
	// direct access to `views` is also allowed
	// this is just for method chaining mainly used immediately after the construction of the view
	VerticalListView *set_views(const std::vector<View *> &views) {
		this->views = views;
		return this;
	}
	VerticalListView *set_draw_order(const std::vector<int> &draw_order) {
		this->draw_order = draw_order;
		return this;
	}
	
	float get_height() const override {
		float res = 0;
		for (auto view : views) res += view->get_height();
		return res;
	}
	void on_scroll() override {
		for (auto view : views) view->on_scroll();
	}
	void draw_() const override {
		if (!draw_order.size()) {
			double y_offset = y0;
			for (auto view : views) {
				double y_bottom = y_offset + view->get_height();
				if (y_bottom >= 0 && y_offset < 240) view->draw(x0, y_offset);
				y_offset = y_bottom;
			}
		} else {
			std::vector<float> y_pos(views.size() + 1, y0);
			for (size_t i = 0; i < views.size(); i++) y_pos[i + 1] = y_pos[i] + views[i]->get_height();
			for (auto i : draw_order) if (y_pos[i + 1] >= 0 && y_pos[i] < 240) views[i]->draw(x0, y_pos[i]);
		}
	}
	void update_(Hid_info key) override {
		double y_offset = y0;
		for (auto view : views) {
			double y_bottom = y_offset + view->get_height();
			if (y_bottom >= 0 && y_offset < 240) view->update(key, x0, y_offset);
			y_offset = y_bottom;
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

