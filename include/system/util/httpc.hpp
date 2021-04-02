#pragma once

Result_with_string Util_httpc_dl_data(std::string url, u8* data_buffer, int buffer_size, u32* downloaded_data_size, u32* status_code, bool follow_redirect,
int max_redirect);

Result_with_string Util_httpc_dl_data(std::string url, u8* data_buffer, int buffer_size, u32* downloaded_data_size, u32* status_code, bool follow_redirect,
int max_redirect, std::string dir_path, std::string file_path);

Result_with_string Util_httpc_dl_data(std::string url, u8* data_buffer, int buffer_size, u32* downloaded_data_size, u32* status_code, bool follow_redirect,
int max_redirect, std::string* last_url);

Result_with_string Util_httpc_dl_data(std::string url, u8* data_buffer, int buffer_size, u32* downloaded_data_size, u32* status_code, bool follow_redirect,
int max_redirect, std::string* last_url, std::string dir_path, std::string file_path);

Result_with_string Util_httpc_post_and_dl_data(std::string url, char* post_data_buffer, int post_buffer_size, u8* dl_data_buffer, int dl_buffer_size,
u32* downloaded_data_size, u32* status_code, bool follow_redirect, int max_redirect);

Result_with_string Util_httpc_post_and_dl_data(std::string url, char* post_data_buffer, int post_buffer_size, u8* dl_data_buffer, int dl_buffer_size,
u32* downloaded_data_size, u32* status_code, bool follow_redirect, int max_redirect, std::string dir_path, std::string file_path);

Result_with_string Util_httpc_post_and_dl_data(std::string url, char* post_data_buffer, int post_buffer_size, u8* dl_data_buffer, int dl_buffer_size,
u32* downloaded_data_size, u32* status_code, bool follow_redirect, int max_redirect, std::string* last_url);

Result_with_string Util_httpc_post_and_dl_data(std::string url, char* post_data_buffer, int post_buffer_size, u8* dl_data_buffer, int dl_buffer_size,
u32* downloaded_data_size, u32* status_code, bool follow_redirect, int max_redirect, std::string* last_url, std::string dir_path, std::string file_path);
