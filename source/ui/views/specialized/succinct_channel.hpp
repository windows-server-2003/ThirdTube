#pragma once
#include <vector>
#include <string>
#include "network_decoder/thumbnail_loader.hpp"
#include "ui/ui_common.hpp"
#include "../view.hpp"

#define CHANNEL_ICON_HEIGHT 54
#define CHANNEL_ICON_WIDTH 96

struct SuccinctChannelView : public FixedSizeView {
private :
	static constexpr double DURATION_FONT_SIZE = 0.4; // todo : make this customizable
	static constexpr double NAME_FONT_SIZE = 0.6; // todo : make this customizable
	static constexpr double NAME_FONT_HEIGHT = 17;
	
	std::string name;
	std::vector<std::string> auxiliary_lines;
public :
	int thumbnail_handle = -1;
	std::string thumbnail_url;
	
	SuccinctChannelView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~SuccinctChannelView () {}
	
	SuccinctChannelView *set_name(const std::string &name) { // mandatory
		this->name = name;
		return this;
	}
	SuccinctChannelView *set_auxiliary_lines(const std::vector<std::string> &auxiliary_lines) { // mandatory
		this->auxiliary_lines = auxiliary_lines;
		return this;
	}
	SuccinctChannelView *set_thumbnail_url(const std::string &thumbnail_url) {
		this->thumbnail_url = thumbnail_url;
		return this;
	}
	
	void draw_() const override {
		Draw_texture(var_square_image[0], DEFAULT_BACK_COLOR, 0, y0, CHANNEL_ICON_WIDTH, y1 - y0);
		thumbnail_draw(thumbnail_handle, x0 + (CHANNEL_ICON_WIDTH - CHANNEL_ICON_HEIGHT) / 2, y0, CHANNEL_ICON_HEIGHT, CHANNEL_ICON_HEIGHT);
		float y = y0;
		Draw(name, x0 + CHANNEL_ICON_WIDTH + 3, y, NAME_FONT_SIZE, NAME_FONT_SIZE, DEFAULT_TEXT_COLOR);
		y += NAME_FONT_HEIGHT;
		for (auto line : auxiliary_lines) {
			Draw(line, x0 + CHANNEL_ICON_WIDTH + 3, y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y += DEFAULT_FONT_INTERVAL;
		}
	}
	void update_(Hid_info key) override {}
};
