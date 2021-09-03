#include "ui/views/specialized/comment.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/log.hpp"
#include "variables.hpp"

void CommentView::draw_() const {
	auto &comment = get_yt_comment_object();
	
	int cur_y = y0;
	if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0) Draw(comment.author.name, x0 + SMALL_MARGIN * 2 + get_icon_size(), cur_y - 3, 0.5, 0.5, DEF_DRAW_GRAY);
	if (cur_y < 240 && cur_y + get_icon_size() > 0) thumbnail_draw(author_icon_handle, x0 + SMALL_MARGIN, cur_y, get_icon_size(), get_icon_size());
	cur_y += DEFAULT_FONT_INTERVAL;
	
	for (size_t line = 0; line < lines_shown; line++) {
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0)
			Draw(content_lines[line], content_x_pos(), cur_y - 2, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		cur_y += DEFAULT_FONT_INTERVAL;
	}
	if (lines_shown < content_lines.size()) {
		cur_y += SMALL_MARGIN;
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0) {
			Draw(LOCALIZED(SHOW_MORE), content_x_pos(), cur_y - 2, 0.5, 0.5, DEF_DRAW_GRAY);
			if (show_more_holding) Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, DEF_DRAW_GRAY,
				content_x_pos() + Draw_get_width(LOCALIZED(SHOW_MORE), 0.5, 0.5), cur_y + DEFAULT_FONT_INTERVAL, DEF_DRAW_GRAY, 1);
		}
		cur_y += DEFAULT_FONT_INTERVAL;
	}
	cur_y = std::max<int>(cur_y, y0 + get_icon_size() + SMALL_MARGIN);
	if (replies_shown) {
		cur_y += SMALL_MARGIN;
		Draw(LOCALIZED(FOLD_REPLIES), content_x_pos(), cur_y - 2, 0.5, 0.5, COLOR_LINK);
		if (fold_replies_holding) Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK,
			content_x_pos() + Draw_get_width(LOCALIZED(FOLD_REPLIES), 0.5, 0.5), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK, 1);
		cur_y += DEFAULT_FONT_INTERVAL;
		cur_y += SMALL_MARGIN;
	}
	for (size_t i = 0; i < replies_shown; i++) {
		static_cast<View *>(replies[i])->draw(0, cur_y);
		cur_y += replies[i]->get_height();
	}
	if (is_loading_replies || comment.has_more_replies() || replies_shown < replies.size()) {
		cur_y += SMALL_MARGIN;
		std::string message = is_loading_replies ? LOCALIZED(LOADING) : replies_shown ? LOCALIZED(SHOW_MORE_REPLIES) : LOCALIZED(SHOW_REPLIES);
		Draw(message, content_x_pos(), cur_y - 2, 0.5, 0.5, COLOR_LINK);
		if (show_more_replies_holding) Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK,
			content_x_pos() + Draw_get_width(message, 0.5, 0.5), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK, 1);
		cur_y += DEFAULT_FONT_INTERVAL;
	}
}
void CommentView::update_(Hid_info key) {
	auto &comment = get_yt_comment_object();
	
	int cur_y = y0;
	bool inside_author_icon = in_range(key.touch_x, x0, std::min<float>(x1, x0 + get_icon_size() + SMALL_MARGIN)) && in_range(key.touch_y, cur_y, cur_y + get_icon_size());
	
	if (key.p_touch && inside_author_icon) icon_holding = true;
	if (key.touch_x == -1 && icon_holding && on_author_icon_pressed_func) on_author_icon_pressed_func(*this);
	if (!inside_author_icon) icon_holding = false;
	
	cur_y += (lines_shown + 1) * DEFAULT_FONT_INTERVAL;
	
	if (lines_shown < content_lines.size()) {
		cur_y += SMALL_MARGIN;
		bool inside_show_more = in_range(key.touch_x, content_x_pos(), std::min<float>(x1, content_x_pos() + Draw_get_width(LOCALIZED(SHOW_MORE), 0.5, 0.5))) &&
			in_range(key.touch_y, cur_y, cur_y + DEFAULT_FONT_INTERVAL);
		
		if (key.p_touch && inside_show_more) show_more_holding = true;
		if (key.touch_x == -1 && show_more_holding) {
			lines_shown = std::min<size_t>(lines_shown + 50, content_lines.size());
			var_need_reflesh = true;
		}
		if (!inside_show_more) show_more_holding = false;
		cur_y += DEFAULT_FONT_INTERVAL;
	}
	
	cur_y = std::max<int>(cur_y, y0 + get_icon_size() + SMALL_MARGIN);
	
	if (replies_shown) {
		cur_y += SMALL_MARGIN;
		bool inside_fold_replies = in_range(key.touch_x, content_x_pos(), std::min<float>(x1, content_x_pos() + Draw_get_width(LOCALIZED(FOLD_REPLIES), 0.5, 0.5))) &&
			in_range(key.touch_y, cur_y, cur_y + DEFAULT_FONT_INTERVAL + 1);
		
		if (key.p_touch && inside_fold_replies) fold_replies_holding = true;
		if (key.touch_x == -1 && fold_replies_holding) {
			replies_shown = 0;
			var_need_reflesh = true;
		}
		if (!inside_fold_replies) fold_replies_holding = false;
		cur_y += DEFAULT_FONT_INTERVAL;
		cur_y += SMALL_MARGIN;
	}
	for (size_t i = 0; i < replies_shown; i++) {
		float cur_height = replies[i]->get_height();
		if (cur_y < 240 && cur_y + cur_height > 0) static_cast<View *>(replies[i])->update(key, 0, cur_y);
		cur_y += cur_height;
	}
	if (is_loading_replies || comment.has_more_replies() || replies_shown < replies.size()) {
		cur_y += SMALL_MARGIN;
		std::string message = is_loading_replies ? LOCALIZED(LOADING) : replies_shown ? LOCALIZED(SHOW_MORE_REPLIES) : LOCALIZED(SHOW_REPLIES);
		bool inside_show_more_replies = in_range(key.touch_x, content_x_pos(), std::min<float>(x1, content_x_pos() + Draw_get_width(message, 0.5, 0.5))) &&
			in_range(key.touch_y, cur_y, cur_y + DEFAULT_FONT_INTERVAL + 1);
		
		if (key.p_touch && inside_show_more_replies) show_more_replies_holding = true;
		if (is_loading_replies) show_more_replies_holding = false;
		if (key.touch_x == -1 && show_more_replies_holding) {
			if (replies_shown < replies.size()) {
				replies_shown = replies.size();
				var_need_reflesh = true;
			} else if (on_load_more_replies_pressed_func) on_load_more_replies_pressed_func(*this);
		}
		if (!inside_show_more_replies) show_more_replies_holding = false;
		cur_y += DEFAULT_FONT_INTERVAL;
	}
}
