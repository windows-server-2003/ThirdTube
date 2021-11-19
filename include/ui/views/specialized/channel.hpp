#pragma once
#include <functional>
#include <vector>
#include <string>
#include "youtube_parser/parser.hpp"
#include "ui/ui_common.hpp"
#include "../view.hpp"
#include "system/util/string_resource.hpp"

#define CHANNEL_ICON_SIZE 55
#define SUBSCRIBE_BUTTON_WIDTH 90
#define SUBSCRIBE_BUTTON_HEIGHT 25

struct ChannelView : public FixedSizeView {
private :
public :
	using CallBackFuncType = std::function<void (const ChannelView &)>;
	
	CallBackFuncType on_subscribe_button_released;
	std::function<bool ()> get_is_subscribed;
	
	ChannelView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~ChannelView () {}
	
	std::string name;
	bool subscribe_button_holding = false;
	int icon_handle = -1;
	
	void reset_holding_status_() override {
		subscribe_button_holding = false;
	}
	void on_scroll() override {
		subscribe_button_holding = false;
	}
	
	ChannelView *set_name(const std::string &name) { // mandatory
		this->name = name;
		return this;
	}
	ChannelView *set_on_subscribe_button_released(CallBackFuncType on_subscribe_button_released) {
		this->on_subscribe_button_released = on_subscribe_button_released;
		return this;
	}
	ChannelView *set_get_is_subscribed(std::function<bool ()> get_is_subscribed) {
		this->get_is_subscribed = get_is_subscribed;
		return this;
	}
	ChannelView *set_icon_handle(int icon_handle) {
		this->icon_handle = icon_handle;
		return this;
	}
	
	void draw_() const override {
		if (get_is_subscribed) {
			thumbnail_draw(icon_handle, x0 + SMALL_MARGIN, y0 + SMALL_MARGIN, CHANNEL_ICON_SIZE, CHANNEL_ICON_SIZE); // icon
			Draw(name, x0 + CHANNEL_ICON_SIZE + SMALL_MARGIN * 3, y0, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR); // channel name
			bool is_subscribed = get_is_subscribed();
			u32 subscribe_button_color = is_subscribed ? LIGHT1_BACK_COLOR : 0xFF4040EE;
			std::string subscribe_button_str = is_subscribed ? LOCALIZED(SUBSCRIBED) : LOCALIZED(SUBSCRIBE);
			float button_x = x1 - SMALL_MARGIN * 2 - SUBSCRIBE_BUTTON_WIDTH;
			float button_y = y0 + SMALL_MARGIN + CHANNEL_ICON_SIZE - SUBSCRIBE_BUTTON_HEIGHT - SMALL_MARGIN;
			Draw_texture(var_square_image[0], subscribe_button_color, button_x, button_y, SUBSCRIBE_BUTTON_WIDTH, SUBSCRIBE_BUTTON_HEIGHT);
			Draw_x_centered(subscribe_button_str, button_x, x1 - SMALL_MARGIN * 2, button_y + 4, 0.5, 0.5, 0xFF000000);
		}
	}
	void update_(Hid_info key) override {
		int subscribe_button_y = y0 + SMALL_MARGIN + CHANNEL_ICON_SIZE - SUBSCRIBE_BUTTON_HEIGHT - SMALL_MARGIN;
		bool in_subscribe_button = key.touch_x >= x1 - SMALL_MARGIN * 2 - SUBSCRIBE_BUTTON_WIDTH && key.touch_x < x1 - SMALL_MARGIN * 2 &&
								   key.touch_y >= subscribe_button_y && key.touch_y < subscribe_button_y + SUBSCRIBE_BUTTON_HEIGHT;
		if (key.p_touch && in_subscribe_button) subscribe_button_holding = true;
		if (subscribe_button_holding && key.touch_x == -1 && on_subscribe_button_released) on_subscribe_button_released(*this);
		if (!in_subscribe_button) subscribe_button_holding = false;
	}
};
