#include "ui/views/specialized/comment.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/log.hpp"
#include "variables.hpp"

void CommentView::draw() const {
	if (y0 < 240 && y0 + DEFAULT_FONT_INTERVAL > 0) Draw(author_name, SMALL_MARGIN * 2 + COMMENT_ICON_SIZE, y0 - 3, 0.5, 0.5, DEF_DRAW_GRAY);
	if (y0 < 240 && y0 + COMMENT_ICON_SIZE > 0) thumbnail_draw(author_icon_handle, SMALL_MARGIN, y0, COMMENT_ICON_SIZE, COMMENT_ICON_SIZE);
	for (size_t line = 0; line < content_lines.size(); line++) {
		int cur_y = y0 + (line + 1) * DEFAULT_FONT_INTERVAL - 2;
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0)
			Draw(content_lines[line], SMALL_MARGIN * 2 + COMMENT_ICON_SIZE, cur_y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
	}
}
void CommentView::update(Hid_info key) {
	bool inside_author_icon = key.touch_x >= 0 && key.touch_x < COMMENT_ICON_SIZE + SMALL_MARGIN && key.touch_y >= y0 && key.touch_y < y0 + COMMENT_ICON_SIZE;
	
	if (key.p_touch && inside_author_icon) holding = true;
	if (key.touch_x == -1 && holding) {
		if (on_author_icon_pressed_func) on_author_icon_pressed_func(*this);
	}
	if (!inside_author_icon) holding = false;
}
