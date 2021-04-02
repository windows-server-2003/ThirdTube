/**
 * @file citro2d.h
 * @brief Central citro2d header. Includes all others.
 */
#pragma once

#ifdef CITRO2D_BUILD
#error "This header file is only for external users of citro2d."
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <citro3d.h>
#include <tex3ds.h>

#include "c2d/base.h"
#include "c2d/spritesheet.h"
#include "c2d/sprite.h"
#include "c2d/text.h"
#include "c2d/font.h"

#ifdef __cplusplus
}
#endif
