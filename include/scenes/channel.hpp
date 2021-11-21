#pragma once

void Channel_init(void);

void Channel_exit(void);

void Channel_suspend(void);

void Channel_resume(std::string arg);

Intent Channel_draw(void);
