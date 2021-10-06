#pragma once

bool Subscription_query_init_flag(void);

void Subscription_resume(std::string arg);

void Subscription_suspend(void);

void Subscription_init(void);

void Subscription_exit(void);

Intent Subscription_draw(void);
