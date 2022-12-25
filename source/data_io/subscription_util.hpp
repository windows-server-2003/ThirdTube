#pragma once
#include <vector>
#include <string>

struct SubscriptionChannel {
	std::string id;
	std::string url;
	std::string name;
	std::string subscriber_count_str;
	std::string icon_url;
	bool valid = true;
};

void load_subscription();
void save_subscription();
bool subscription_is_subscribed(const std::string &id);
void subscription_subscribe(const SubscriptionChannel &channel);
void subscription_unsubscribe(const std::string &id);
std::vector<SubscriptionChannel> get_valid_subscribed_channels();
