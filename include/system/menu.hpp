#pragma once

bool Menu_query_must_exit_flag(void);

void Menu_set_must_exit_flag(bool flag);

void Menu_resume(void);

void Menu_suspend(void);

void Menu_init(void);

void Menu_exit(void);

void Menu_main(void);

void Menu_get_system_info(void);

int Menu_check_free_ram(void);

void Menu_send_app_info_thread(void* arg);

void Menu_check_connectivity_thread(void* arg);

void Menu_worker_thread(void* arg);

void Menu_update_thread(void* arg);
