#pragma once

void Subscription_init(void);

void Subscription_exit(void);

void Subscription_suspend(void);

void Subscription_resume(std::string arg);

Intent Subscription_draw(void);
