#include "ui/dialog.hpp"
#include "ui/colors.hpp"
#include "variables.hpp"
#include "headers.hpp"

#define MARGIN 7

Dialog::Dialog(int x_l, int x_r, int y_l, int y_r, std::string message, std::vector<std::string> button_strings) : message(message), button_strings(button_strings) {
	change_area(x_l, x_r, y_l, y_r);
	message_height = Draw_get_height(message, 0.5, 0.5) + 2 * MARGIN;
	message_width = Draw_get_width(message, 0.5, 0.5) + 2 * MARGIN;
	for (auto &i : button_strings) button_height = std::max(button_height, Draw_get_height(i, 0.5, 0.5) + 2 * MARGIN);
}
int Dialog::button_at(int x, int y) {
	float dialog_y = y_l + (float) (y_r - y_l - (message_height + button_height)) / 2;
	float dialog_width = std::max<float>(message_width, (x_r - x_l) * 0.6);
	float dialog_x = x_l + (x_r - x_l - dialog_width) / 2;
	if (x < dialog_x || x >= dialog_x + dialog_width) return -1;
	if (y < dialog_y + message_height || y >= dialog_y + message_height + button_height) return -1;
	int res = (x - dialog_x) * button_strings.size() / dialog_width;
	res = std::max(res, 0); // just to be safe, I'm afraid of the error of floating point numbers
	res = std::min(res, (int) button_strings.size() - 1);
	return res;
}
int Dialog::update(Hid_info key) {
	int res = -1;
	if (key.p_touch && button_at(key.touch_x, key.touch_y) != -1) {
		holding_button = true;
	}
	if (holding_button && key.touch_x == -1) res = button_at(last_touch_x, last_touch_y);
	if (key.touch_x == -1 && holding_button) var_need_reflesh = true;
	
	last_touch_x = key.touch_x;
	last_touch_y = key.touch_y;
	if (key.touch_x == -1) holding_button = false;
	return res;
}
void Dialog::draw() {
	Draw_texture(var_square_image[0], 0x40000000, x_l, y_l, x_r - x_l, y_r - y_l);
	
	float y = y_l + (float) (y_r - y_l - (message_height + button_height)) / 2;
	float width = std::max<float>(message_width, (x_r - x_l) * 0.6);
	float x = x_l + (x_r - x_l - width) / 2;
	// background
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, x, y, width, message_height + button_height);
	if (holding_button && button_at(last_touch_x, last_touch_y) != -1) { // indicates the selected button
		int id = button_at(last_touch_x, last_touch_y);
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, x + id * width / button_strings.size(), y + message_height,
			width / button_strings.size(), button_height);
	}
	// main message
	{
		float string_width = Draw_get_width(message, 0.5, 0.5);
		float string_height = Draw_get_height(message, 0.5, 0.5);
		Draw(message, x + (width - string_width) / 2, y + (message_height - string_height) / 2 - 3, 0.5, 0.5, DEF_DRAW_BLACK);
	}
	Draw_line(x, y + message_height, DEF_DRAW_GRAY, x + width, y + message_height, DEF_DRAW_GRAY, 1);
	for (int i = 0; i < (int) button_strings.size(); i++) {
		float cur_x = x + (float) width * i / button_strings.size();
		float string_width = Draw_get_width(button_strings[i], 0.5, 0.5);
		float string_height = Draw_get_height(button_strings[i], 0.5, 0.5);
		float cur_width = width / button_strings.size();
		Draw(button_strings[i], cur_x + (cur_width - string_width) / 2, y + message_height + (button_height - string_height) / 2 - 3, 0.5, 0.5, DEF_DRAW_BLACK);
		if (i) Draw_line(cur_x, y + message_height, DEF_DRAW_GRAY, cur_x, y + message_height + button_height, DEF_DRAW_GRAY, 1);
	}
}
