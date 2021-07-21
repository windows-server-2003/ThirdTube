#pragma once
#include "types.hpp"
#include <vector>
#include <string>

class Dialog {
	// area of the scroller
	int x_l = 0;
	int x_r = 0;
	int y_l = 0;
	int y_r = 0;
	std::string message;
	std::vector<std::string> button_strings;
	float message_height = 0;
	float message_width = 0;
	float button_height = 0;
	
	bool holding_button = false;
	int last_touch_x = -1;
	int last_touch_y = -1;
	int button_at(int x, int y);
public :
	Dialog () = default;
	Dialog (int x_l, int x_r, int y_l, int y_r, std::string message, std::vector<std::string> button_strings);
	
	void change_area(int x_l, int x_r, int y_l, int y_r) {
		this->x_l = x_l;
		this->x_r = x_r;
		this->y_l = y_l;
		this->y_r = y_r;
	}
	// if a button is pressed, returns the button index
	// otherwise, returns -1
	int update(Hid_info key);
	void draw();
};

