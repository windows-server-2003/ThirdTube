#pragma once
#include "types.hpp"
#include "system/util/log.hpp"

struct View {
protected :
	double x0 = 0;
	double y0 = 0;
public :
	View (double x0, double y0) : x0(x0), y0(y0) {}
	virtual ~View () {}
	
	virtual void on_scroll() {}
	virtual void recursive_delete_subviews() {}
	virtual void add_offset(double x_offset, double y_offset) {
		x0 += x_offset;
		y0 += y_offset;
	}
	virtual float get_width() const = 0;
	virtual float get_height() const = 0;
	virtual void draw() const = 0;
	virtual void draw(double x_offset, double y_offset) {
		add_offset(x_offset, y_offset);
		draw();
		add_offset(-x_offset, -y_offset);
	}
	virtual void update(Hid_info key) = 0;
	virtual void update(Hid_info key, double x_offset, double y_offset) {
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

