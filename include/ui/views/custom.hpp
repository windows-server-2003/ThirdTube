#pragma once
#include <string>
#include "view.hpp"
#include "../ui_common.hpp"

struct CustomView : public FixedSizeView {
public :
	CustomView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~CustomView () {}
	
	std::function<void (const CustomView &)> draw_func;
	std::function<void (CustomView &)> update_func;
	
	CustomView *set_draw(const std::function<void (const CustomView &)> &draw_func) {
		this->draw_func = draw_func;
		return this;
	}
	CustomView *set_update(const std::function<void (CustomView &)> &update_func) {
		this->update_func = update_func;
		return this;
	}
	
	void draw_() const override {
		draw_func(*this);
	}
	void update_(Hid_info key) override {
		if (update_func) update_func(*this);
	}
};
