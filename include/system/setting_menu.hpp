#pragma once

bool Sem_query_init_flag(void);

bool Sem_query_running_flag(void);

void Sem_suspend(void);

void Sem_resume(std::string arg);

void Sem_init(void);

void Sem_exit(void);

Intent Sem_draw(void);

void Sem_encode_thread(void* arg);

void Sem_record_thread(void* arg);

void Sem_worker_thread(void* arg);

void Sem_update_thread(void* arg);
