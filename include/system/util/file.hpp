#pragma once

Result_with_string Util_file_save_to_file(std::string file_name, std::string dir_path, u8* write_data, int size, bool delete_old_file);

Result_with_string Util_file_load_from_file(std::string file_name, std::string dir_path, u8* read_data, int max_size, u32* read_size);

Result_with_string Util_file_load_from_rom(std::string file_name, std::string dir_path, u8* read_data, int max_size, u32* read_size);

Result_with_string Util_file_load_from_file_with_range(std::string file_name, std::string dir_path, u8* read_data, int read_length, u64 read_offset, u32* read_size);

Result_with_string Util_file_delete_file(std::string file_name, std::string dir_path);

Result_with_string Util_file_check_file_size(std::string file_name, std::string dir_path, u64* file_size);

Result_with_string Util_file_check_file_exist(std::string file_name, std::string dir_path);

Result_with_string Util_file_read_dir(std::string dir_path, int* num_of_detected, std::string file_and_dir_name[], int name_num_of_array, std::string type[], int type_num_of_array);
