#pragma once
#include <vector>
#include <functional>
#include <utility>
#include "types.hpp"
#include "system/draw/draw.hpp"
#include "system/util/log.hpp"
#include "variables.hpp"

struct View {
private :
	static constexpr double TOUCH_DARKNESS_SPEED = 0.1;
protected :
	std::function<void (View &view)> on_view_released;
	std::function<void (View &view)> on_drawn;
public :
	View (double x0, double y0) : x0(x0), y0(y0) {}
	virtual ~View () {}
	
	double x0 = 0;
	double y0 = 0;
	u32 background_color = 0;
	bool scrolled = false;
	int view_holding_time = 0;
	double touch_darkness = 0;
	std::function<u32 (const View &)> get_background_color;
	bool is_visible = true;
	bool is_touchable = true;
	std::vector<std::pair<int, std::function<void (View &view)> > > on_long_holds;
	
	const static std::function<u32 (const View &)> STANDARD_BACKGROUND;
	
	virtual View *set_on_view_released(std::function<void (View &view)> on_view_released) {
		this->on_view_released = on_view_released;
		return this;
	}
	virtual View *set_on_drawn(std::function<void (View &view)> on_drawn) {
		this->on_drawn = on_drawn;
		return this;
	}
	virtual View *set_background_color(u32 color) {
		this->background_color = color;
		return this;
	}
	virtual View *set_get_background_color(std::function<u32 (const View &)> get_background_color) {
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
	virtual View *add_on_long_hold(int frames, const std::function<void (View &view)> &func) {
		this->on_long_holds.push_back({frames, func});
		return this;
	}
	
	virtual void reset_holding_status() {
		view_holding_time = 0;
		reset_holding_status_();
	}
	virtual void reset_holding_status_() {}
	virtual void on_scroll() {
		view_holding_time = 0;
		scrolled = true;
	}
	virtual void recursive_delete_subviews() {}
	virtual void add_offset(double x_offset, double y_offset) {
		x0 += x_offset;
		y0 += y_offset;
	}
	virtual float get_width() const = 0;
	virtual float get_height() const = 0;
	virtual void draw_background() const {
		u32 color = get_background_color ? get_background_color(*this) : background_color;
		if (color >> 24) Draw_texture(var_square_image[0], color, (int) x0, (int) y0, (int) get_width(), (int) get_height());
	}
	void draw() const {
		// Draw_line(x0, y0, 0xFF000000, x0 + get_width(), y0, 0xFF000000, 1);
		if (is_visible) {
			draw_background();
			draw_();
		}
	}
	virtual void draw_() const = 0;
	void draw(double x_offset, double y_offset) {
		add_offset(x_offset, y_offset);
		if (on_drawn) on_drawn(*this);
		draw();
		add_offset(-x_offset, -y_offset);
	}
	virtual void update_(Hid_info key) = 0;
	void update(Hid_info key) {
		if (is_touchable) {
			bool inside_view = key.touch_x >= x0 && key.touch_x < x0 + get_width() &&
				key.touch_y >= y0 && key.touch_y < y0 + get_height();
			if (inside_view && (key.p_touch || view_holding_time)) {
				view_holding_time++;
				if (!scrolled) touch_darkness = std::min(1.0, touch_darkness + TOUCH_DARKNESS_SPEED);
				else touch_darkness = std::max(0.0, touch_darkness - TOUCH_DARKNESS_SPEED);
				for (auto on_long_hold : on_long_holds) if (on_long_hold.first == view_holding_time)
					on_long_hold.second(*this);
			} else touch_darkness = std::max(0.0, touch_darkness - TOUCH_DARKNESS_SPEED);
			if (key.touch_x == -1 && view_holding_time && on_view_released) on_view_released(*this);
			if (!inside_view) view_holding_time = 0;
			update_(key);
		}
		scrolled = false;
	}
	void update(Hid_info key, double x_offset, double y_offset) {
		add_offset(x_offset, y_offset);
		update(key);
		add_offset(-x_offset, -y_offset);
	}
};

struct FixedWidthView : virtual public View {
public :
	FixedWidthView (double x0, double y0, double width) : View(x0, y0), width(width), x1(x0 + width) {}
	virtual ~FixedWidthView () {}
	
	double width = 0;
	double x1 = 0;
	
	void add_offset(double x_offset, double y_offset) override {
		View::add_offset(x_offset, y_offset);
		x1 += x_offset;
	}
	FixedWidthView *update_x_range(double x0, double x1) {
		this->x0 = x0;
		this->x1 = x1;
		this->width = x1 - x0;
		return this;
	}
	float get_width() const override { return width; }
	void set_width(float width) { this->width = width; this->x1 = this->x0 + width; }
};

struct FixedHeightView : virtual public View {
public :
	FixedHeightView (double x0, double y0, double height) : View(x0, y0), height(height), y1(y0 + height) {}
	virtual ~FixedHeightView () {}
	
	double height = 0;
	double y1 = 0;
	
	void add_offset(double x_offset, double y_offset) override {
		View::add_offset(x_offset, y_offset);
		y1 += y_offset;
	}
	FixedHeightView *update_y_range(double y0, double y1) {
		this->y0 = y0;
		this->y1 = y1;
		this->height = y1 - y0;
		return this;
	}
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

