#pragma once

/*
 * OcarinaStudio - game headers (C) for the modules that touch PlayState / Player.
 * Same include pattern used by the other Ship of Harkinian enhancements (see BunnyHood.cpp).
 *
 * The C++ headers must come first: if the C game headers were the first to pull in the standard
 * library, it would end up inside the extern "C" block below and fail to compile.
 */

#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}
