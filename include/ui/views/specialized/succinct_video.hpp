#pragma once
#include <vector>
#include <string>
#include "network/thumbnail_loader.hpp"
#include "ui/ui_common.hpp"
#include "../view.hpp"

#define VIDEO_LIST_THUMBNAIL_HEIGHT 54
#define VIDEO_LIST_THUMBNAIL_WIDTH 96

struct SuccinctVideoView : public FixedSizeView {
private :
	static constexpr double DURATION_FONT_SIZE = 0.4; // todo : make this customizable
	
	std::vector<std::string> title_lines;
	std::vector<std::string> auxiliary_lines;
	std::string bottom_right_overlay;
	float get_thumbnail_height() const { return y1 - y0; }
	float get_thumbnail_width() const { return get_thumbnail_height() * VIDEO_LIST_THUMBNAIL_WIDTH / VIDEO_LIST_THUMBNAIL_HEIGHT; }
public :
	int thumbnail_handle = -1;
	std::string thumbnail_url;
	
	bool is_playlist = false;
	
	SuccinctVideoView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~SuccinctVideoView () {}
	
	SuccinctVideoView *set_title_lines(const std::vector<std::string> &title_lines) { // mandatory
		this->title_lines = title_lines;
		return this;
	}
	SuccinctVideoView *set_auxiliary_lines(const std::vector<std::string> &auxiliary_lines) { // mandatory
		this->auxiliary_lines = auxiliary_lines;
		return this;
	}
	SuccinctVideoView *set_bottom_right_overlay(const std::string &bottom_right_overlay) {
		this->bottom_right_overlay = bottom_right_overlay;
		return this;
	}
	SuccinctVideoView *set_thumbnail_url(const std::string &thumbnail_url) {
		this->thumbnail_url = thumbnail_url;
		return this;
	}
	SuccinctVideoView *set_is_playlist(bool is_playlist) {
		this->is_playlist = is_playlist;
		return this;
	}
	float get_title_width() const { return x1 - x0 - (get_thumbnail_width() + 3); }
	
	void draw_() const override {
		thumbnail_draw(thumbnail_handle, x0, y0, get_thumbnail_width(), get_thumbnail_height());
		if (bottom_right_overlay.size()) {
			float overlay_width = Draw_get_width(bottom_right_overlay, DURATION_FONT_SIZE, DURATION_FONT_SIZE);
			Draw_texture(var_square_image[0], 0xBB000000, x0 + get_thumbnail_width() - overlay_width - 2, y0 + get_thumbnail_height() - 10, overlay_width + 2, 10);
			Draw(bottom_right_overlay, x0 + get_thumbnail_width() - overlay_width - 1, y0 + get_thumbnail_height() - 11,
				DURATION_FONT_SIZE, DURATION_FONT_SIZE, (u32) -1);
		}
		if (is_playlist) {
			double overlay_proportion = 0.3;
			Draw_texture(var_square_image[0], 0xA0000000, x0 + get_thumbnail_width() * (1 - overlay_proportion), y0,
				get_thumbnail_width() * overlay_proportion, get_thumbnail_height());
			Draw_x_centered(LOCALIZED(PLAYLIST_SHORT), x0 + get_thumbnail_width() * (1 - overlay_proportion), x0 + get_thumbnail_width(),
				(y1 + y0) / 2 - 8, 0.5, 0.5, (u32) -1);
		}
		float y = y0;
		for (auto line : title_lines) {
			Draw(line, x0 + get_thumbnail_width() + 3, y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y += DEFAULT_FONT_INTERVAL;
		}
		y += 2;
		for (auto line : auxiliary_lines) {
			Draw(line, x0 + get_thumbnail_width() + 3, y, 0.5, 0.5, LIGHT0_TEXT_COLOR);
			y += DEFAULT_FONT_INTERVAL;
		}
	}
	void update_(Hid_info key) override {
	}
};
