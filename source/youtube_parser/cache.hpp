#pragma once
#include "n_param.hpp"
#include "cipher.hpp"

std::string yt_procs_to_string(const yt_cipher_transform_procedure &cipher_proc, const std::string &nparam_func);
bool yt_procs_from_string(const std::string &str, yt_cipher_transform_procedure &cipher_proc, std::string &nparam_func);

