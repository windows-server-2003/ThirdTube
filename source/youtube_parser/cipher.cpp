#include "cipher.hpp"
#include "internal_common.hpp"
#include <algorithm>
#include <sstream>

enum OpType {
	SWAP_ZERO, // s
	REVERSE, // R
	ROTATE, // r
	CUT, // c
	ERASE, // e
	TRANSFORM // t
};
static OpType str2optype(const std::string &str) {
	if (str == "s") return SWAP_ZERO;
	if (str == "R") return REVERSE;
	if (str == "r") return ROTATE;
	if (str == "c") return CUT;
	if (str == "e") return ERASE;
	if (str == "t") return TRANSFORM;
	return (OpType) -1;
}
struct Op {
	OpType type;
	int arg_int;
	std::string arg_str;
	std::string arg2_str;
};

static int sts;
static std::vector<Op> nparam_ops;
static std::vector<Op> sig_ops;

void youtube_set_cipher_decrypter(std::string decrypter) {
	std::stringstream stream(decrypter);
	
	std::vector<Op> nparam_ops;
	std::vector<Op> sig_ops;
	bool switched = false;
	int sts = -1;
	while (stream) {
		std::string op_str;
		if (!(stream >> op_str)) break;
		std::vector<Op> &cur_ops = (switched ? nparam_ops : sig_ops);
		if (op_str == "s" || op_str == "r" || op_str == "c" || op_str == "e") {
			int arg;
			if (!(stream >> arg)) {
				debug("failed to read arg for " + op_str);
				break;
			}
			cur_ops.push_back({str2optype(op_str), arg, "", ""});
		} else if (op_str == "R") cur_ops.push_back({str2optype(op_str), 0, "", ""});
		else if (op_str == "t") {
			std::string arg1, arg2;
			if (!(stream >> arg1 >> arg2)) {
				debug("failed to read arg for " + op_str);
				break;
			}
			cur_ops.push_back({str2optype(op_str), 0, arg1, arg2});
		} else if (op_str == ">") switched ^= 1; // switch between nparam and sig
		else if (op_str == "#") {
			if (!(stream >> sts)) {
				debug("failed to read sts num");
				break;
			}
		} 
	}
	if (nparam_ops.size() && sig_ops.size() && sts != -1) {
		::nparam_ops = nparam_ops;
		::sig_ops = sig_ops;
		::sts = sts;
		debug("loaded decrypter (sts : " + std::to_string(sts) + ")");
	} else debug("nparam or sig ops empty");
}


static std::string yt_transform(std::string s, const std::vector<Op> &ops) {
	auto get_mod = [] (int x, int y) { return (x % y + y) % y; };
	auto swap_zero = [&] (int index) {
		std::swap(s[0], s[get_mod(index, s.size())]);
	};
	auto rotate = [&] (int num) {
		std::rotate(s.begin(), s.end() - get_mod(num, s.size()), s.end());
	};
	auto reverse = [&] () { std::reverse(s.begin(), s.end()); };
	auto cut = [&] (int index) { s.erase(s.begin(), s.begin() + index); };
	auto erase = [&] (int index) { s.erase(s.begin() + get_mod(index, s.size())); };
	auto transform = [&] (std::string a, std::string sample) {
		auto f = [&sample] (int index) -> char {
			for (auto s : sample) {
				int n = s == '0' ? 10 : (s == 'a' || s == 'A') ? 26 : 1;
				if (index < n) return s + index;
				index -= n;
			}
			return '?';
		};
		auto f_rev = [&sample] (char c) -> int {
			int cur = 0;
			for (auto s : sample) {
				int n = s == '0' ? 10 : (s == 'a' || s == 'A') ? 26 : 1;
				if (c >= s && c < s + n) return cur + c - s;
				cur += n;
			}
			return -1;
		};
		for (size_t i = 0; i < s.size(); i++) {
			int index = f_rev(s[i]) - f_rev(a[i]);
			if (index < 0) index += 64;
			a.push_back(s[i] = f(index));
		}
	};
	
	for (auto op : ops) {
		if (op.type == SWAP_ZERO) swap_zero(op.arg_int);
		if (op.type == REVERSE) reverse();
		if (op.type == ROTATE) rotate(op.arg_int);
		if (op.type == CUT) cut(op.arg_int);
		if (op.type == ERASE) erase(op.arg_int);
		if (op.type == TRANSFORM) transform(op.arg_str, op.arg2_str);
	}
	
	return s;
}
std::string nparam_transform(std::string s) { return yt_transform(s, nparam_ops); }
std::string sig_transform(std::string s) { return yt_transform(s, sig_ops); }
int get_sts() { return sts; }
