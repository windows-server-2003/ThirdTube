#pragma once

enum class SceneType {
	VIDEO_PLAYER,
	SEARCH,
	SETTING,
	// used for intent
	NO_CHANGE,
	EXIT
};
struct Intent {
	SceneType next_scene;
	std::string arg;
};


void Menu_init(void);

void Menu_exit(void);

bool Menu_main(void);

void Menu_get_system_info(void);

int Menu_check_free_ram(void);
