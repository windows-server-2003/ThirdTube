#include "ui/views/specialized/comment.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/log.hpp"
#include "variables.hpp"

void CommentView::draw() const {
	if (y0 < 240 && y0 + DEFAULT_FONT_INTERVAL > 0) Draw(author_name, SMALL_MARGIN * 2 + COMMENT_ICON_SIZE, y0 - 3, 0.5, 0.5, DEF_DRAW_GRAY);
	if (y0 < 240 && y0 + COMMENT_ICON_SIZE > 0) thumbnail_draw(author_icon_handle, SMALL_MARGIN, y0, COMMENT_ICON_SIZE, COMMENT_ICON_SIZE);
	for (size_t line = 0; line < lines_shown; line++) {
		int cur_y = y0 + (line + 1) * DEFAULT_FONT_INTERVAL - 2;
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0)
			Draw(content_lines[line], content_x_pos(), cur_y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
	}
	if (lines_shown < content_lines.size()) {
		int cur_y = y0 + (lines_shown + 1) * DEFAULT_FONT_INTERVAL + SMALL_MARGIN;
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0) {
			Draw(LOCALIZED(SHOW_MORE), content_x_pos(), cur_y - 2, 0.5, 0.5, DEF_DRAW_GRAY);
			if (show_more_holding) Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, DEF_DRAW_GRAY,
				content_x_pos() + Draw_get_width(LOCALIZED(SHOW_MORE), 0.5, 0.5), cur_y + DEFAULT_FONT_INTERVAL, DEF_DRAW_GRAY, 1);
		}
	}
}
void CommentView::update(Hid_info key) {
	bool inside_author_icon = is_inside_author_icon(key.touch_x, key.touch_y);
	
	if (key.p_touch && inside_author_icon) icon_holding = true;
	if (key.touch_x == -1 && icon_holding && on_author_icon_pressed_func) on_author_icon_pressed_func(*this);
	if (!inside_author_icon) icon_holding = false;
	
	bool inside_show_more = is_inside_show_more_button(key.touch_x, key.touch_y);
	if (key.p_touch && inside_show_more) show_more_holding = true;
	if (key.touch_x == -1 && show_more_holding) {
		lines_shown = std::min<size_t>(lines_shown + 50, content_lines.size());
		var_need_reflesh = true;
	}
	if (!inside_show_more) show_more_holding = false;
	
}
