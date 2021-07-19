#pragma once

enum class SceneType {
	VIDEO_PLAYER,
	SEARCH,
	SETTING,
	CHANNEL,
	// used for intent
	NO_CHANGE,
	BACK,
	EXIT
};
struct Intent {
	SceneType next_scene;
	std::string arg;
	
	bool operator == (const Intent &rhs) {
		return next_scene == rhs.next_scene && arg == rhs.arg;
	}
};


void Menu_init(void);

void Menu_exit(void);

bool Menu_main(void);

void Menu_get_system_info(void);

int Menu_check_free_ram(void);
