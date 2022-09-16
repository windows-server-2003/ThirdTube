#pragma once

Result_with_string Util_swkbd_set_dic_word(std::string first_spell[], std::string full_spell[], int num_of_word);

void Util_swkbd_init(SwkbdType type, SwkbdValidInput valid_type, int num_of_button, int max_length, std::string hint_text, std::string init_text);

void Util_swkbd_set_password_mode(SwkbdPasswordMode password_mode);

void Util_swkbd_set_feature(u32 feature);

std::string Util_swkbd_launch(int max_length, std::string* out_data);
