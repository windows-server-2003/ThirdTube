#pragma once
#include "view.hpp"
#include <vector>

struct ListView : public View {
public :
	using View::View;
	
	std::vector<View *> views;
	
	// direct access to `views` is also allowed
	// this is just for method chaining mainly used immediately after the construction of the view
	ListView *set_views(const std::vector<View *> &views) {
		this->views = views;
		return this;
	}
	
	void draw() const override {
		double y_offset = y0;
		for (auto view : views) {
			view->draw(x0, y_offset);
			y_offset += view->y1 - view->y0;
		}
	}
	void update(Hid_info key) override {
		double y_offset = y0;
		for (auto view : views) {
			view->update(key, x0, y_offset);
			y_offset += view->y1 - view->y0;
		}
	}
};

