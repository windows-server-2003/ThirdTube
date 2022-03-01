#pragma once
#include <functional>
#include <vector>
#include <string>
#include "system/util/util.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui_common.hpp"
#include "../view.hpp"

#define CAPTION_OVERLAY_MAX_WIDTH 280

// drawn on the top screen
struct CaptionOverlayView : public FixedSizeView {
private :
	static constexpr int OVERLAY_SIDE_MARGIN = 3;
	struct CaptionPiece {
		float start_time;
		float end_time;
		std::vector<std::string> lines; // wrapped
	};
	std::vector<CaptionPiece> caption_data;
public :
	using CallBackFuncType = std::function<void (const CaptionOverlayView &)>;
	
	CaptionOverlayView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {
		is_touchable = false;
	}
	virtual ~CaptionOverlayView () {}
	
	float cur_timestamp = 0;
	
	CaptionOverlayView *set_caption_data(const std::vector<YouTubeVideoDetail::CaptionPiece> &caption_data) { // mandatory
		this->caption_data.clear();
		for (auto caption_piece : caption_data) {
			auto &cur_content = caption_piece.content;
			if (cur_content == "" || cur_content == "\n") continue;
			
			CaptionPiece cur_piece;
			cur_piece.start_time = caption_piece.start_time;
			cur_piece.end_time = caption_piece.end_time;
			
			std::vector<std::string> cur_lines;
			auto itr = cur_content.begin();
			while (itr != cur_content.end()) {
				if (cur_lines.size() >= 10) break;
				auto next_itr = std::find(itr, cur_content.end(), '\n');
				auto tmp = truncate_str(std::string(itr, next_itr), CAPTION_OVERLAY_MAX_WIDTH, 10 - cur_lines.size(), 0.5, 0.5);
				cur_lines.insert(cur_lines.end(), tmp.begin(), tmp.end());
				
				if (next_itr != cur_content.end()) itr = std::next(next_itr);
				else break;
			}
			cur_piece.lines = cur_lines;
			
			this->caption_data.push_back(cur_piece);
		}
		return this;
	}
	
	void draw_() const override {
		int start_pos;
		{
			int l = -1;
			int r = caption_data.size();
			while (r - l > 1) {
				int m = l + ((r - l) >> 1);
				if (caption_data[m].end_time < cur_timestamp) l = m;
				else r = m;
			}
			start_pos = r;
		}
		std::vector<std::string> lines;
		for (size_t i = start_pos; i < caption_data.size() && caption_data[i].start_time < cur_timestamp; i++) {
			auto &cur_lines = caption_data[i].lines;
			lines.insert(lines.end(), cur_lines.begin(), cur_lines.end());
		}
		while (lines.size() && lines.back() == "") lines.pop_back();
		
		float start_y = 240 - 10 - DEFAULT_FONT_INTERVAL * lines.size();
		for (size_t i = 0; i < lines.size(); i++) {
			float width = Draw_get_width(lines[i], 0.5);
			
			Draw_texture(var_square_image[0], 0xBB000000, (400 - width) / 2 - OVERLAY_SIDE_MARGIN, start_y + i * DEFAULT_FONT_INTERVAL,
				width + OVERLAY_SIDE_MARGIN * 2, DEFAULT_FONT_INTERVAL);
			Draw(lines[i], (400 - width) / 2, start_y + i * DEFAULT_FONT_INTERVAL - 2, 0.5, 0.5, (u32) -1);
		}
	}
	void update_(Hid_info key) override {}
};
