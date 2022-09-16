#pragma once
#include "view.hpp"
#include "scene_switcher.hpp"
#include <vector>

struct VerticalListView : public FixedWidthView {
public :
	VerticalListView (double x0, double y0, double width) : View(x0, y0), FixedWidthView(x0, y0, width) {}
	virtual ~VerticalListView () {}
	
	std::vector<View *> views;
	std::vector<int> draw_order;
	double margin = 0.0;
	bool do_thumbnail_update = false;
		// these are thumbnail-related variables, not used if do_thumbnail_update == false
		int thumbnail_loaded_l = 0;
		int thumbnail_loaded_r = 0;
		int thumbnail_max_request = 1;
		SceneType thumbnail_scene;
	
	void reset_holding_status_() override {
		for (auto view : views) view->reset_holding_status();
	}
	virtual void recursive_delete_subviews() override;
	void swap_views(const std::vector<View *> &new_views); // replaces `views` with `new_views` while trying to avoid thumbnail reloading as much as possible
	
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
	VerticalListView *set_margin(double margin) {
		this->margin = margin;
		return this;
	}
	VerticalListView *enable_thumbnail_request_update(int thumbnail_max_request, SceneType scene_type) {
		this->do_thumbnail_update = true;
		this->thumbnail_scene = scene_type;
		this->thumbnail_max_request = thumbnail_max_request;
		return this;
	}
	
	float get_height() const override {
		float res = 0;
		for (auto view : views) res += view->get_height();
		res += std::max((int) views.size() - 1, 0) * margin;
		return res;
	}
	void on_scroll() override {
		for (auto view : views) view->on_scroll();
	}
	
	void draw_() const override;
	void update_(Hid_info key) override;
};

