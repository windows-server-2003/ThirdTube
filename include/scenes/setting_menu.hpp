#pragma once

void Sem_init(void);

void Sem_exit(void);

void Sem_suspend(void);

void Sem_resume(std::string arg);

Intent Sem_draw(void);
