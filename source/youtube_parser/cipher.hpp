#pragma once

using yt_cipher_transform_procedure = std::vector<std::pair<int, int> >;

yt_cipher_transform_procedure yt_get_transform_plan(const std::string &js);
std::string yt_deobfuscate_signature(std::string sig, const yt_cipher_transform_procedure &transform_plan);
