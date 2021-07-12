#include "headers.hpp"
#include <set>

static bool cpu_limits_lock_inited = false;
static Handle cpu_limits_lock;
static std::multiset<int> cpu_limits;

void add_cpu_limit(int limit) {
	if (!cpu_limits_lock_inited) {
		svcCreateMutex(&cpu_limits_lock, false);
		cpu_limits_lock_inited = true;
	}
	svcWaitSynchronization(cpu_limits_lock, std::numeric_limits<s64>::max());
	cpu_limits.insert(limit);
	APT_SetAppCpuTimeLimit(*cpu_limits.begin());
	svcReleaseMutex(cpu_limits_lock);
}
void remove_cpu_limit(int limit) {
	svcWaitSynchronization(cpu_limits_lock, std::numeric_limits<s64>::max());
	cpu_limits.erase(cpu_limits.find(limit));
	APT_SetAppCpuTimeLimit(*cpu_limits.begin());
	svcReleaseMutex(cpu_limits_lock);
}
int get_cpu_limit() {
	u32 res;
	APT_GetAppCpuTimeLimit(&res);
	return res;
}

