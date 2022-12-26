#pragma once
#include "types.hpp"
#include <string>
#include <cinttypes>

struct Path {
	std::string path;
	Path () = default;
	Path (const std::string &path) : path(path) {}
	Result_with_string write_file(const u8 *data, u32 size);
	Result_with_string read_file(u8 *data, u32 size, u32 &size_read, u64 offset = 0);
	template<typename T> Result_with_string read_entire_file(T &resulting_data) {
		u64 size;
		Result_with_string res = get_size(size);
		if (res.code != 0) return res;
		resulting_data.resize(size);
		u32 size_read;
		return read_file((u8 *) &resulting_data[0], size, size_read, 0);
	}
	Result_with_string delete_file();
	Result_with_string rename_to(const std::string &new_path);
	Result_with_string get_size(u64 &res);
	bool is_file();
	Result_with_string read_dir(std::string *names, std::string *types, int max_num, int &read_num);
};
struct AtomicFileIO {
	std::string main_path;
	std::string tmp_path;
	AtomicFileIO (const std::string &main_path, const std::string &tmp_path) : main_path(main_path), tmp_path(tmp_path) {}
	
	template<typename T> std::pair<Result_with_string, std::string> load(const T &check_is_valid) {
		std::string buf;
		// first try loading from temporary path
		Result_with_string res = Path(tmp_path).read_entire_file(buf);
		if (res.code == 0 && check_is_valid(buf)) {
			// do not handle errors on these operations, because they are not critical
			Path(main_path).delete_file();
			Path(tmp_path).rename_to(main_path);
			return {res, buf};
		}
		res = Path(main_path).read_entire_file(buf);
		return {res, buf};
	}
	Result_with_string save(const std::string &data) {
		Result_with_string res = Path(tmp_path).write_file((const u8 *) data.data(), data.size());
		if (res.code != 0) return res;
		// do not handle errors on these operations, because they are not critical
		Path(main_path).delete_file();
		Path(tmp_path).rename_to(main_path);
		return res;
	}
};
