#pragma once
#include "view.hpp"
#include "../ui_common.hpp"
#include "network/thumbnail_loader.hpp"

struct ImageView : public FixedSizeView {
public :
	ImageView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~ImageView () {}
	
	int handle = -1;
	
	ImageView *set_handle(int handle) {
		this->handle = handle;
		return this;
	}
	
	void draw_() const override {
		thumbnail_draw(handle, x0, y0, x1 - x0, y1 - y0);
	}
	void update_(Hid_info key) override {}
};
