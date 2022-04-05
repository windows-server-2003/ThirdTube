#include "hardcode.hpp"
#include <algorithm>

std::string nparam_transform(std::string s) {
	auto get_mod = [] (int x, int y) { return (x % y + y) % y; };
	auto swap_zero = [&] (int index) {
		std::swap(s[0], s[get_mod(index, s.size())]);
	};
	auto rotate = [&] (int num) {
		std::rotate(s.begin(), s.end() - get_mod(num, s.size()), s.end());
	};
	auto reverse = [&] () { std::reverse(s.begin(), s.end()); };
	auto remove = [&] (int index) { s.erase(s.begin() + get_mod(index, s.size())); };
	auto transform = [&] (std::string a) {
		auto f = [] (int index) -> char {
			if (index < 10) return '0' + index;
			if (index < 10 + 26) return 'a' + index - 10;
			if (index < 10 + 26 + 26) return 'A' + index - (10 + 26);
			if (index == 10 + 26 + 26) return '-';
			return '_';
		};
		auto f_rev = [] (char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'z') return c - 'a' + 10;
			if (c >= 'A' && c <= 'Z') return c - 'A' + 10 + 26;
			if (c == '-') return 10 + 26 + 26;
			return 10 + 26 + 26 + 1; // '_'
		};
		for (size_t i = 0; i < s.size(); i++) {
			int index = f_rev(s[i]) - f_rev(a[i]);
			if (index < 0) index += 64;
			a.push_back(s[i] = f(index));
		}
	};
	
	reverse();
	swap_zero(-838262480);
	swap_zero(-1680559646);
	swap_zero(218749902);
	swap_zero(1711147982);
	swap_zero(-523498913);
	swap_zero(1861473240);
	swap_zero(444815830);
	reverse();
	rotate(-657910602);
	rotate(-463360672);
	rotate(-1128978876);
	remove(1548207588);
	swap_zero(-1856601180);
	swap_zero(-211035849);
	rotate(-703167729);
	transform("indexOf");
	remove(914060385);
	swap_zero(1722454367);
	rotate(-1946431023);
	
	return s;
}
std::string cipher_transform(std::string s) {
	std::swap(s[0], s[34 % s.size()]);
	s.erase(s.begin(), s.begin() + 2);
	std::swap(s[0], s[28 % s.size()]);
	std::reverse(s.begin(), s.end());
	std::swap(s[0], s[58 % s.size()]);
	
	return s;
}
