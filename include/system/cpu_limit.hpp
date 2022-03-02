#pragma once

#define NEW_3DS_CPU_LIMIT 50
#define OLD_3DS_CPU_LIMIT 70
#define CPU_LIMIT (var_is_new3ds ? NEW_3DS_CPU_LIMIT : OLD_3DS_CPU_LIMIT)

#define NEW_3DS_ADDITIONAL_CPU_LIMIT 25
#define OLD_3DS_ADDITIONAL_CPU_LIMIT 55
#define ADDITIONAL_CPU_LIMIT (var_is_new3ds ? NEW_3DS_ADDITIONAL_CPU_LIMIT : OLD_3DS_ADDITIONAL_CPU_LIMIT)


void add_cpu_limit(int limit);

// there must have been corresponding adding
void remove_cpu_limit(int limit);

int get_cpu_limit();
