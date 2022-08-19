#include <cstring>
#include "common.hpp"


std::string double2str(double value, int digits_low) {
	char buf[20];
	snprintf(buf, 20, "%.*f", digits_low, value);
	return buf;
}
