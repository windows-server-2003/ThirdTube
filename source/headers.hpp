#pragma once

#include <3ds.h>
#include <algorithm>
#include <cstring>
#include <malloc.h>
#include <string>
#include <unistd.h>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cinttypes>
#include "citro2d.h"

#include "definitions.hpp"
#include "ui/draw/draw.hpp"
#include "ui/draw/external_font.hpp"
#include "system/change_setting.hpp"
#include "system/file.hpp"
#include "system/hid.hpp"
#include "system/speaker.hpp"
#include "system/libctru_wrapper.hpp"
#include "system/apt_handler.hpp"
#include "util/explorer.hpp"
#include "util/log.hpp"
#include "util/error.hpp"
// #include "system/camera.hpp"
#include "network_decoder/converter.hpp"
#include "network_decoder/image.hpp"
#include "data_io/string_resource.hpp"
// #include "system/mic.hpp"
// #include "system/util/swkbd.hpp"
#include "util/util.hpp"
#include "variables.hpp"
#include "scene_switcher.hpp"
#include "types.hpp"
#include "system/cpu_limit.hpp"
#include "common.hpp"
