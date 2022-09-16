#pragma once
#include "types.hpp"
#include <string>
#include <cinttypes>

struct Path {
	std::string path;
	Path () = default;
	Path (const std::string &path) : path(path) {}
	Result_with_string write_file(u8 *data, u32 size);
	Result_with_string read_file(u8 *data, u32 size, u32 &size_read, u64 offset = 0);
	Result_with_string delete_file();
	Result_with_string get_size(u64 &res);
	bool is_file();
	Result_with_string read_dir(std::string *names, std::string *types, int max_num, int &read_num);
};
