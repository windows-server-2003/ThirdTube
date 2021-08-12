#pragma once
#include "n_param.hpp"
#include "cipher.hpp"

std::string yt_procs_to_string(const yt_cipher_transform_procedure &cipher_proc, const yt_nparam_transform_procedure &nparam_proc);
bool yt_procs_from_string(const std::string &str, yt_cipher_transform_procedure &cipher_proc, yt_nparam_transform_procedure &nparam_proc);

