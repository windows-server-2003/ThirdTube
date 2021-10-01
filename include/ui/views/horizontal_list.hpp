#pragma once
#include "view.hpp"
#include <vector>

struct HorizontalListView : public FixedHeightView {
public :
	HorizontalListView (double x0, double y0, double height) : View(x0, y0), FixedHeightView(x0, y0, height) {}
	virtual ~HorizontalListView () {}
	
	std::vector<View *> views;
	std::vector<int> draw_order;
	
	virtual void recursive_delete_subviews() override {
		for (auto view : views) {
			view->recursive_delete_subviews();
			delete view;
		}
		views.clear();
	}
	
	// direct access to `views` is also allowed
	// this is just for method chaining mainly used immediately after the construction of the view
	HorizontalListView *set_views(const std::vector<View *> &views) {
		this->views = views;
		return this;
	}
	HorizontalListView *set_draw_order(const std::vector<int> &draw_order) {
		this->draw_order = draw_order;
		return this;
	}
	
	float get_width() const override {
		float res = 0;
		for (auto view : views) res += view->get_width();
		return res;
	}
	void reset_holding_status_() override {
		for (auto view : views) view->reset_holding_status();
	}
	void on_scroll() override {
		for (auto view : views) view->on_scroll();
	}
	void draw_() const override {
		if (!draw_order.size()) {
			double x_offset = x0;
			for (auto view : views) {
				double x_right = x_offset + view->get_width();
				if (x_right >= 0 && x_offset < 320) view->draw(x_offset, y0);
				x_offset = x_right;
			}
		} else {
			std::vector<float> x_pos(views.size() + 1, x0);
			for (size_t i = 0; i < views.size(); i++) x_pos[i + 1] = x_pos[i] + views[i]->get_width();
			for (auto i : draw_order) if (x_pos[i + 1] >= 0 && x_pos[i] < 320) views[i]->draw(x_pos[i], y0);
		}
	}
	void update_(Hid_info key) override {
		double x_offset = x0;
		for (auto view : views) {
			double x_right = x_offset + view->get_width();
			if (x_right >= 0 && x_offset < 320) view->update(key, x_offset, y0);
			x_offset = x_right;
		}
	}
};

