#include "headers.hpp"
#include "unicodetochar/unicodetochar.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

Result_with_string Path::write_file(const u8 *data, u32 size) {
	Result_with_string res;
	res.string = [&] () -> std::string {
		FILE *fp = fopen(path.c_str(), "w");
		if (!fp) {
			const char *begin = path.c_str();
			const char *slash = strchr(begin + 1, '/');
			while (slash) {
				errno = 0;
				if (mkdir(path.substr(0, slash - begin).c_str(), 0777) && errno != EEXIST) return "mkdir() failed";
				slash = strchr(slash + 1, '/');
			}
			errno = 0;
			fp = fopen(path.c_str(), "w");
		}
		if (!fp) return "fopen() failed even after mkdir: ";
		errno = 0;
		u32 written = fwrite(data, 1, size, fp);
		fclose(fp);
		if (written < size) return "fwrite() failed(" + std::to_string(written) + " < " + std::to_string(size) + ")";
		return "";
	}();
	if (res.string != "") res.code = errno;
	return res;
}
Result_with_string Path::read_file(u8 *data, u32 size, u32 &size_read, u64 offset) {
	Result_with_string res;
	res.string = [&] () {
		errno = 0;
		FILE *fp = fopen(path.c_str(), "rb");
		if (!fp) return "fopen() failed";
		auto tmp = [&] () {
			if (offset) {
				errno = 0;
				if (fseek(fp, offset, SEEK_SET) != 0) return "fseek() failed";
			}
			errno = 0;
			size_read = fread(data, 1, size, fp);
			if (!size_read) return "fread() failed";
			return "";
		}();
		fclose(fp);
		return tmp;
	}();
	if (res.string != "") res.code = errno;
	return res;
}
Result_with_string Path::delete_file() {
	Result_with_string res;
	errno = 0;
	if (remove(path.c_str()) != 0) res.string = "remove() failed", res.code = errno;
	return res;
}
Result_with_string Path::rename_to(const std::string &new_name) {
	Result_with_string res;
	errno = 0;
	if (rename(path.c_str(), new_name.c_str()) != 0) res.string = "rename() failed", res.code = errno;
	return res;
}
Result_with_string Path::get_size(u64 &size) {
	Result_with_string res;
	res.string = [&] () {
		errno = 0;
		FILE *fp = fopen(path.c_str(), "rb");
		if (!fp) return "fopen() failed";
		auto tmp = [&] () {
			errno = 0;
			if (fseek(fp, 0, SEEK_END) != 0) return "fseek() failed";
			errno = 0;
			long ftell_res = ftell(fp);
			if (ftell_res < 0) return "ftell() failed";
			size = ftell_res;
			return "";
		}();
		fclose(fp);
		return tmp;
	}();
	if (res.string != "") res.code = errno;
	return res;
}
bool Path::is_file() {
	FILE *fp = fopen(path.c_str(), "rb");
	if (!fp) return false;
	fclose(fp);
	return true;
}
Result_with_string Path::read_dir(std::string *names, std::string *types, int max_num, int &read_num) {
	Result_with_string res;
	res.string = [&] () {
		errno = 0;
		DIR *dir = opendir(path.c_str());
		if (!dir) return "opendir() failed";
		[&] () {
			struct dirent *item = readdir(dir);
			int cnt = 0;
			while (item && cnt < max_num) {
				names[cnt] = item->d_name;
				types[cnt] = (item->d_type == DT_REG ? "file" : item->d_type == DT_DIR ? "dir" : "unknown");
				cnt++;
				item = readdir(dir);
			}
			read_num = cnt;
		}();
		closedir(dir);
		return "";
	}();
	if (res.string != "") res.code = errno;
	return res;
}
