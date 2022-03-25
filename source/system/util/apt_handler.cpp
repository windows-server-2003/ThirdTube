#include "headers.hpp"
#include "scenes/video_player.hpp"

static aptHookCookie apt_hook_cookie;
static void apt_callback(APT_HookType hook, void* param){
	if (hook == APTHOOK_ONSUSPEND) {
		if (!var_is_new3ds) {
			video_set_should_suspend_decoding(true);
			add_cpu_limit(30);
		}
	} else if (hook == APTHOOK_ONRESTORE) {
		if (!var_is_new3ds) {
			video_set_should_suspend_decoding(false);
			remove_cpu_limit(30);
		}
	}
}

void set_apt_callback() {
	aptHook(&apt_hook_cookie, apt_callback, NULL);
}
void remove_apt_callback() {
	aptUnhook(&apt_hook_cookie);
}
