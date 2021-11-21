#pragma once

void History_init(void);

void History_exit(void);

void History_suspend(void);

void History_resume(std::string arg);

Intent History_draw(void);
