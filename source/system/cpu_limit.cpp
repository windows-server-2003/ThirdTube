#include "headers.hpp"
#include <set>

static Mutex cpu_limits_lock;
static std::multiset<int> cpu_limits;

void add_cpu_limit(int limit) {
	cpu_limits_lock.lock();
	cpu_limits.insert(limit);
	APT_SetAppCpuTimeLimit(*cpu_limits.begin());
	cpu_limits_lock.unlock();
}
void remove_cpu_limit(int limit) {
	cpu_limits_lock.lock();
	cpu_limits.erase(cpu_limits.find(limit));
	APT_SetAppCpuTimeLimit(*cpu_limits.begin());
	cpu_limits_lock.unlock();
}
int get_cpu_limit() {
	u32 res;
	APT_GetAppCpuTimeLimit(&res);
	return res;
}

