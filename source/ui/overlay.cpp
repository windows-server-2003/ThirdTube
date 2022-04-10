#include "ui/overlay.hpp"
#include "variables.hpp"
#include "headers.hpp"
#include "ui/ui.hpp"



#define ICON_LINE_HEIGHT 3
#define ICON_LINE_Y_MARGIN 4
#define ICON_LINE_X_MARGIN 5

#define CONTENT_HEIGHT 25
#define CONTENT_WIDTH 120

struct Content {
	std::string string;
	enum class Type {
		SEARCH,
		HISTORY,
		HOME,
		EXIT,
		SETTINGS,
		ABOUT,
	};
	Type type;
};
static std::vector<Content> contents;

enum MenuStatus {
	CLOSED,
	OPEN,
	CONFIRMING_CLOSE,
};
static int menu_status = CLOSED;
static bool is_menu_drawn() { return menu_status == OPEN || menu_status == CONFIRMING_CLOSE; }

static int menu_icon_x = 320 - OVERLAY_MENU_ICON_SIZE;
static int menu_icon_y;

static int last_touch_x = -1;
static int last_touch_y = -1;
static bool holding_touch = false;

static OverlayDialogView *dialog_view = (new OverlayDialogView(0, 0, 320, 240));

#define menu_icon_background_color             COLOR_GRAY(var_night_mode ? 0x3F : 0xC0)
#define menu_icon_background_color_selected    COLOR_GRAY(var_night_mode ? 0x5F : 0xA0)
#define menu_icon_line_color                   COLOR_GRAY(var_night_mode ? 0xCC : 0x33)
#define menu_content_background_color          menu_icon_background_color
#define menu_content_background_color_selected COLOR_GRAY(var_night_mode ? 0x5F : 0xA0)
#define menu_content_string_color              DEFAULT_TEXT_COLOR
#define menu_content_border_color              COLOR_GRAY(var_night_mode ? 0x22 : 0xDD)

static bool in_icon(int x, int y) {
	return x >= menu_icon_x && x < menu_icon_x + OVERLAY_MENU_ICON_SIZE && y >= menu_icon_y && y < menu_icon_y + OVERLAY_MENU_ICON_SIZE;
}
static int content_at(int x, int y) {
	if (x < 320 - CONTENT_WIDTH) return -1;
	int y_base = menu_icon_y - contents.size() * CONTENT_HEIGHT;
	if (y < y_base || y >= menu_icon_y) return -1;
	return (y - y_base) / CONTENT_HEIGHT;
}

void draw_overlay_menu(int y) {
	menu_icon_y = y;
	
	u32 color = holding_touch && in_icon(last_touch_x, last_touch_y) ? menu_icon_background_color_selected : menu_icon_background_color;
	Draw_texture(var_square_image[0], color, menu_icon_x, menu_icon_y, OVERLAY_MENU_ICON_SIZE, OVERLAY_MENU_ICON_SIZE);
	for (int i = 0; i < 3; i++) {
		int y = menu_icon_y + (float) (OVERLAY_MENU_ICON_SIZE - ICON_LINE_HEIGHT * 3 - ICON_LINE_Y_MARGIN * 2) / 2 + i * (ICON_LINE_HEIGHT + ICON_LINE_Y_MARGIN);
		int x = menu_icon_x + ICON_LINE_X_MARGIN;
		int width = OVERLAY_MENU_ICON_SIZE - ICON_LINE_X_MARGIN * 2;
		Draw_texture(var_square_image[0], menu_icon_line_color, x, y, width, ICON_LINE_HEIGHT);
	}
	
	if (is_menu_drawn()) {
		int y_base = menu_icon_y - contents.size() * CONTENT_HEIGHT;
		for (int i = 0; i < (int) contents.size(); i++) {
			color = holding_touch && content_at(last_touch_x, last_touch_y) == i ? menu_content_background_color_selected : menu_content_background_color;
			Draw_texture(var_square_image[0], color, 320 - CONTENT_WIDTH, y_base + i * CONTENT_HEIGHT, CONTENT_WIDTH, CONTENT_HEIGHT);
			int string_width = Draw_get_width(contents[i].string, 0.5);
			int string_x = 320 - CONTENT_WIDTH + (CONTENT_WIDTH - string_width) / 2;
			Draw(contents[i].string, string_x, y_base + i * CONTENT_HEIGHT + 5, 0.5, 0.5, menu_content_string_color);
		}
	}
	if (menu_status == CONFIRMING_CLOSE) dialog_view->draw();
}
void update_overlay_menu(Hid_info *key) {
	static bool exit_confirmed = false;
	static SceneType prev_scene = SceneType::SEARCH;
	if (prev_scene != global_current_scene) {
		var_need_reflesh = true;
		prev_scene = global_current_scene;
	}
	contents.clear();
	if (global_current_scene != SceneType::HOME) contents.push_back({LOCALIZED(HOME), Content::Type::HOME});
	if (global_current_scene != SceneType::SEARCH) contents.push_back({LOCALIZED(GOTO_SEARCH), Content::Type::SEARCH});
	if (global_current_scene != SceneType::HISTORY) contents.push_back({LOCALIZED(WATCH_HISTORY), Content::Type::HISTORY});
	contents.push_back({LOCALIZED(EXIT_APP), Content::Type::EXIT});
	if (global_current_scene != SceneType::SETTINGS) contents.push_back({LOCALIZED(SETTINGS), Content::Type::SETTINGS});
	if (global_current_scene != SceneType::ABOUT) contents.push_back({LOCALIZED(ABOUT), Content::Type::ABOUT});
	
	if (menu_status == CONFIRMING_CLOSE) {
		holding_touch = false;
		var_need_reflesh = true;
		
		dialog_view->update(*key);
		
		key->touch_x = key->touch_y = -1;
		key->p_touch = false;
	} else {
		if (holding_touch && key->touch_x == -1 && last_touch_x != -1) {
			if (in_icon(last_touch_x, last_touch_y)) {
				if (menu_status == CLOSED) menu_status = OPEN;
				else menu_status = CLOSED;
			} else if (menu_status != CLOSED && content_at(last_touch_x, last_touch_y) != -1) {
				int id = content_at(last_touch_x, last_touch_y);
				if (contents[id].type == Content::Type::SEARCH) {
					global_intent.next_scene = SceneType::SEARCH;
					global_intent.arg = "";
				} else if (contents[id].type == Content::Type::EXIT) {
					dialog_view->get_message_view()->set_text([] () { return LOCALIZED(EXIT_CONFIRM); })->update_y_range(0, 30);
					dialog_view->set_buttons<std::function<std::string ()> >({
						[] () { return LOCALIZED(CANCEL); },
						[] () { return LOCALIZED(EXIT_APP); }
					}, [] (OverlayDialogView &, int button_pressed) {
						if (button_pressed == 0) menu_status = OPEN;
						else if (button_pressed == 1) exit_confirmed = true;
						return true; // close
					});
					dialog_view->set_is_visible(true);
					dialog_view->set_on_cancel([] (OverlayView &view) {
						menu_status = OPEN;
						view.set_is_visible(false);
						var_need_reflesh = true;
					});
					var_need_reflesh = true;
					menu_status = CONFIRMING_CLOSE;
				} else if (contents[id].type == Content::Type::ABOUT) {
					global_intent.next_scene = SceneType::ABOUT;
					global_intent.arg = "";
				} else if (contents[id].type == Content::Type::SETTINGS) {
					global_intent.next_scene = SceneType::SETTINGS;
					global_intent.arg = "";
				} else if (contents[id].type == Content::Type::HISTORY) {
					global_intent.next_scene = SceneType::HISTORY;
					global_intent.arg = "";
				} else if (contents[id].type == Content::Type::HOME) {
					global_intent.next_scene = SceneType::HOME;
					global_intent.arg = "";
				}
			} else menu_status = CLOSED;
			var_need_reflesh = true;
		}

		if (holding_touch && last_touch_x != -1 && key->touch_x == -1) var_need_reflesh = true;
		
		
		if (key->p_touch) {
			if (in_icon(key->touch_x, key->touch_y)) holding_touch = true;
			else if (menu_status != CLOSED && content_at(key->touch_x, key->touch_y) != -1) holding_touch = true;
			else menu_status = CLOSED;
		}
		
		last_touch_x = key->touch_x;
		last_touch_y = key->touch_y;
		
		if (key->touch_x == -1) holding_touch = false;
		if (holding_touch) {
			key->touch_x = key->touch_y = -1;
			key->p_touch = false;
		}
	}
	if (exit_confirmed) {
		exit_confirmed = false;
		global_intent.next_scene = SceneType::EXIT;
		global_intent.arg = "";
	}
}
void close_overlay_menu() {
	menu_status = CLOSED;
	var_need_reflesh = true;
}
void overlay_menu_on_resume() {
	last_touch_x = -1;
	last_touch_y = -1;
	menu_status = CLOSED;
	holding_touch = false;
	var_need_reflesh = true;
}

