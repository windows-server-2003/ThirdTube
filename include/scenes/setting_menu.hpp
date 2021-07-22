#pragma once

bool Sem_query_init_flag(void);

void Sem_suspend(void);

void Sem_resume(std::string arg);

void Sem_init(void);

void Sem_exit(void);

Intent Sem_draw(void);
