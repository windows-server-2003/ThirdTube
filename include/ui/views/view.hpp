#pragma once
#include "types.hpp"

struct View {
	double x0 = 0;
	double y0 = 0;
	double x1 = 0;
	double y1 = 0;
	
	View (double x0, double y0, double x_size, double y_size) : x0(x0), y0(y0), x1(x0 + x_size), y1(y0 + y_size) {}
	void update_x_range(double x0, double x1) { this->x0 = x0; this->x1 = x1; }
	void update_y_range(double y0, double y1) { this->y0 = y0; this->y1 = y1; }
	void add_offset(double x_offset, double y_offset) {
		x0 += x_offset;
		x1 += x_offset;
		y0 += y_offset;
		y1 += y_offset;
	}
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

