#pragma once
#include "ui/ui.hpp"

void Search_init(void);

void Search_exit(void);

void Search_suspend(void);

void Search_resume(std::string arg);

void Search_draw(void);

TextView *Search_get_toast_view();
View *Search_get_search_bar_view();

bool Search_show_url_input_keyboard();
bool Search_show_search_keyboard();
