#pragma once

//settings
#define DEF_MAIN_DIR (std::string)"/3ds/ThirdTube/"
#define DEF_UPDATE_DIR_PREFIX (std::string)"/3ds/Video_player_ver_"
#define DEF_UPDATE_FILE_PREFIX (std::string)"video_player"
#define DEF_SWKBD_MAX_DIC_WORDS 128
#define DEF_CHECK_INTERNET_URL (std::string)"https://connectivitycheck.gstatic.com/generate_204"
#define DEF_SEND_APP_INFO_URL (std::string)"https://script.google.com/macros/s/AKfycbyn_blFyKWXCgJr6NIF8x6ETs7CHRN5FXKYEAAIrzV6jPYcCkI/exec"
#define DEF_CHECK_UPDATE_URL (std::string)"https://script.google.com/macros/s/AKfycbwTd3jzV0npUE9MNKmZIv3isazVR5D9_7A8rexsG1vr9SE7iavDBxgtzlph8dZipwu9/exec"
#define DEF_HTTP_USER_AGENT (std::string)"video player for 3ds v1.1.0"
#define DEF_CURRENT_APP_VER (std::string)"0.5.0"
#define DEF_CURRENT_APP_VER_INT 0
#define GITHUB_URL std::string("https://github.com/windows-server-2003/ThirdTube")

//video player
#define DEF_SAPP0_NAME (std::string)"Video\nplayer"
#define DEF_SAPP0_MAIN_STR (std::string)"Vid/Main"
#define DEF_SAPP0_INIT_STR (std::string)"Vid/Init"
#define DEF_SAPP0_EXIT_STR (std::string)"Vid/Exit"
#define DEF_SAPP0_DECODE_THREAD_STR (std::string)"Vid/Decode thread"
#define DEF_SAPP0_CONVERT_THREAD_STR (std::string)"Vid/Convert thread"

//menu
#define DEF_MENU_MAIN_STR (std::string)"Menu/Main"
#define DEF_MENU_INIT_STR (std::string)"Menu/Init"
#define DEF_MENU_EXIT_STR (std::string)"Menu/Exit"
#define DEF_MENU_WORKER_THREAD_STR (std::string)"Menu/Worker thread"
#define DEF_MENU_UPDATE_THREAD_STR (std::string)"Menu/Update thread"
#define DEF_MENU_SEND_APP_INFO_STR (std::string)"Menu/Send app info thread"
#define DEF_MENU_CHECK_INTERNET_STR (std::string)"Menu/Check internet thread"

//setting menu
#define DEF_SEM_INIT_STR (std::string)"Sem/Init"
#define DEF_SEM_EXIT_STR (std::string)"Sem/Exit"
#define DEF_SEM_WORKER_THREAD_STR (std::string)"Sem/Worker thread"
#define DEF_SEM_UPDATE_THREAD_STR (std::string)"Sem/Update thread"
#define DEF_SEM_ENCODE_THREAD_STR (std::string)"Sem/Encode thread"
#define DEF_SEM_RECORD_THREAD_STR (std::string)"Sem/Record thread"

//explorer
#define DEF_EXPL_INIT_STR (std::string)"Expl/Init"
#define DEF_EXPL_EXIT_STR (std::string)"Expl/Exit"
#define DEF_EXPL_READ_DIR_THREAD_STR (std::string)"Expl/Read dir thread"

//external font
#define DEF_EXFONT_NUM_OF_FONT_NAME 50
#define DEF_EXFONT_INIT_STR (std::string)"Exfont/Init"
#define DEF_EXFONT_EXIT_STR (std::string)"Exfont/Exit"
#define DEF_EXFONT_LOAD_FONT_THREAD_STR (std::string)"Exfont/Load font thread"

//error num
#define DEF_ERR_SUMMARY 0
#define DEF_ERR_DESCRIPTION 1
#define DEF_ERR_PLACE 2
#define DEF_ERR_CODE 3

//error code
#define DEF_ERR_OTHER 0xFFFFFFFF
#define DEF_ERR_OUT_OF_MEMORY 0xFFFFFFFE
#define DEF_ERR_OUT_OF_LINEAR_MEMORY 0xFFFFFFFD
#define DEF_ERR_GAS_RETURNED_NOT_SUCCESS 0xFFFFFFFC
#define DEF_ERR_STB_IMG_RETURNED_NOT_SUCCESS 0xFFFFFFFB
#define DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS 0xFFFFFFFA
#define DEF_ERR_INVALID_ARG 0xFFFFFFF9
#define DEF_ERR_NEED_MORE_INPUT 0xFFFFFFF8
#define DEF_ERR_NEED_MORE_OUTPUT 0xFFFFFFF7

#define DEF_ERR_OTHER_STR (std::string)"[Error] Something went wrong. "
#define DEF_ERR_OUT_OF_MEMORY_STR (std::string)"[Error] Out of memory. "
#define DEF_ERR_OUT_OF_LINEAR_MEMORY_STR (std::string)"[Error] Out of linear memory. "
#define DEF_ERR_GAS_RETURNED_NOT_SUCCESS_STR (std::string)"[Error] Google apps script returned NOT success. "
#define DEF_ERR_STB_IMG_RETURNED_NOT_SUCCESS_STR (std::string)"[Error] stb image returned NOT success. "
#define DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR (std::string)"[Error] ffmpeg returned NOT success. "
#define DEF_ERR_INVALID_ARG_STR (std::string)"[Error] Invalid arg. "

//thread
#define DEF_STACKSIZE (64 * 1024)
#define DEF_INACTIVE_THREAD_SLEEP_TIME 100000
#define DEF_ACTIVE_THREAD_SLEEP_TIME 50000
//0x18~0x3F
#define DEF_THREAD_PRIORITY_IDLE 0x36
#define DEF_THREAD_PRIORITY_LOW 0x25
#define DEF_THREAD_PRIORITY_NORMAL 0x24
#define DEF_THREAD_PRIORITY_HIGH 0x23
#define DEF_THREAD_PRIORITY_REALTIME 0x18
