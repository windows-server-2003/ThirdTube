#pragma once

void add_cpu_limit(int limit);

// there must have been corresponding adding
void remove_cpu_limit(int limit);

int get_cpu_limit();
