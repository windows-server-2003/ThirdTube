#pragma once
#include <functional>
#include "view.hpp"

// for margin
struct OverlayView : public FixedSizeView {
public :
	using CallBackFuncType = std::function<void (const OverlayView &)>;
	
	OverlayView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~OverlayView () {}
	
	View *subview = NULL;
	
	double content_x() const { return x0 + (x1 - x0 - subview->get_width()) / 2; }
	double content_y() const { return y0 + (y1 - y0 - subview->get_height()) / 2; }
	
	bool holding_outside = false;
	CallBackFuncType on_cancel_func;
	
	OverlayView *set_subview(View *subview) {
		this->subview = subview;
		return this;
	}
	OverlayView *set_on_cancel(CallBackFuncType on_cancel_func) {
		this->on_cancel_func = on_cancel_func;
		return this;
	}
	void on_scroll() override {
		subview->on_scroll();
	}
	void reset_holding_status_() override {
		holding_outside = false;
	}
	void recursive_delete_subviews() override {
		if (subview) subview->recursive_delete_subviews();
		delete subview;
		subview = NULL;
	}
	
	void draw_() const override {
		Draw_texture(var_square_image[0], 0x40000000, x0, y0, x1 - x0, y1 - y0);
		subview->draw(content_x(), content_y());
	}
	void update_(Hid_info key) override {
		bool in_outside = key.touch_x >= x0 && key.touch_x < x1 && key.touch_y >= y0 && key.touch_y < y1 && 
			!(key.touch_x >= content_x() && key.touch_x < content_x() + subview->get_width() &&
				key.touch_y >= content_y() && key.touch_y < content_y() + subview->get_height());
		if (key.p_touch && in_outside) holding_outside = true;
		if (key.touch_x == -1 && holding_outside) on_cancel_func(*this);
		if (!in_outside) holding_outside = false;
		
		subview->update(key, content_x(), content_y());
	}
};
