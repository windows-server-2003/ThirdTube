#pragma once
#include <functional>
#include "types.hpp"
#include "system/draw/draw.hpp"
#include "system/util/log.hpp"
#include "variables.hpp"

struct View {
protected :
	double x0 = 0;
	double y0 = 0;
	int view_holding_time = 0;
	std::function<void (View &view)> on_view_released;
public :
	View (double x0, double y0) : x0(x0), y0(y0) {}
	virtual ~View () {}
	
	u32 background_color = 0;
	std::function<u32 (int)> get_background_color;
	bool is_visible = true;
	bool is_touchable = true;
	
	virtual View *set_on_view_released(std::function<void (View &view)> on_view_released) {
		this->on_view_released = on_view_released;
		return this;
	}
	virtual View *set_background_color(u32 color) {
		this->background_color = color;
		return this;
	}
	virtual View *set_get_background_color(std::function<u32 (int)> get_background_color) {
		this->get_background_color = get_background_color;
		return this;
	}
	virtual View *set_is_visible(bool is_visible) {
		this->is_visible = is_visible;
		return this;
	}
	virtual View *set_is_touchable(bool is_touchable) {
		this->is_touchable = is_touchable;
		return this;
	}
	
	virtual void on_scroll() { view_holding_time = 0; }
	virtual void recursive_delete_subviews() {}
	virtual void add_offset(double x_offset, double y_offset) {
		x0 += x_offset;
		y0 += y_offset;
	}
	virtual float get_width() const = 0;
	virtual float get_height() const = 0;
	virtual void draw_background() const {
		u32 color = get_background_color ? get_background_color(view_holding_time) : background_color;
		if (color >> 24) Draw_texture(var_square_image[0], color, x0, y0, get_width(), get_height());
	}
	void draw() const {
		if (is_visible) {
			draw_background();
			draw_();
		}
	}
	virtual void draw_() const = 0;
	void draw(double x_offset, double y_offset) {
		add_offset(x_offset, y_offset);
		draw();
		add_offset(-x_offset, -y_offset);
	}
	virtual void update_(Hid_info key) = 0;
	void update(Hid_info key) {
		if (is_touchable) {
			bool inside_view = key.touch_x >= x0 && key.touch_x < x0 + get_width() &&
				key.touch_y >= y0 && key.touch_y < y0 + get_height();
			if (inside_view && (key.p_touch || view_holding_time)) view_holding_time++;
			if (key.touch_x == -1 && view_holding_time && on_view_released) on_view_released(*this);
			if (!inside_view) view_holding_time = 0;
			update_(key);
		}
	}
	void update(Hid_info key, double x_offset, double y_offset) {
		add_offset(x_offset, y_offset);
		update(key);
		add_offset(-x_offset, -y_offset);
	}
};

struct FixedWidthView : virtual public View {
protected :
	double width = 0;
	double x1 = 0;
public :
	FixedWidthView (double x0, double y0, double width) : View(x0, y0), width(width), x1(x0 + width) {}
	virtual ~FixedWidthView () {}
	
	void add_offset(double x_offset, double y_offset) override {
		View::add_offset(x_offset, y_offset);
		x1 += x_offset;
	}
	void update_x_range(double x0, double x1) { this->x0 = x0; this->x1 = x1; this->width = x1 - x0; }
	float get_width() const override { return width; }
	void set_width(float width) { this->width = width; this->x1 = this->x0 + width; }
};

struct FixedHeightView : virtual public View {
protected :
	double height = 0;
	double y1 = 0;
public :
	FixedHeightView (double x0, double y0, double height) : View(x0, y0), height(height), y1(y0 + height) {}
	virtual ~FixedHeightView () {}
	
	void add_offset(double x_offset, double y_offset) override {
		View::add_offset(x_offset, y_offset);
		y1 += y_offset;
	}
	void update_y_range(double y0, double y1) { this->y0 = y0; this->y1 = y1; this->height = y1 - y0; }
	float get_height() const override { return height; }
	void set_height(float height) { this->height = height; this->y1 = this->y0 + height; }
};


struct FixedSizeView : public FixedWidthView, public FixedHeightView {
public :
	FixedSizeView (double x0, double y0, double width, double height) : View(x0, y0), FixedWidthView(x0, y0, width), FixedHeightView(x0, y0, height) {}
	virtual ~FixedSizeView () {}
	
	void add_offset(double x_offset, double y_offset) override {
		View::add_offset(x_offset, y_offset);
		x1 += x_offset;
		y1 += y_offset;
	}
};

