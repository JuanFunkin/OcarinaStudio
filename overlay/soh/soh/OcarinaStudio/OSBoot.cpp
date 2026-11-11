#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include "functions.h"
void Sram_InitDebugSave(void);
}

/*
 * Boot straight into the studio.
 *
 * OcarinaStudio does not use the logos, the title screen or the file select. As soon as the game starts it
 * builds a temporary save, loads Link's House and turns on the green screen stage with no HUD. Worlds are
 * then chosen from the Game menu at the top of the window.
 *
 * Set gOcarinaStudio.BootToStage to 0 in the config file to get the normal title screen back.
 */

namespace OcarinaStudio {

// OcarinaAnimator.exe / OcarinaAnimator.sh start the same program with OCARINA_ANIMATOR set. In that mode the
// game opens on the green screen with Link standing still and the Animator window ready.
bool IsAnimatorMode() {
    static const bool sAnimatorMode = std::getenv("OCARINA_ANIMATOR") != nullptr;
    return sAnimatorMode;
}

} // namespace OcarinaStudio

namespace {

constexpr int kStartEntrance = OcarinaStudio::kStageEntrance;

void BootToStage(void* gameState) {
    if (CVarGetInteger("gOcarinaStudio.BootToStage", 1) == 0) {
        return;
    }
    auto* title = static_cast<TitleContext*>(gameState);

    // The studio always starts on the clean green screen stage. 30 FPS is the default (version 2 of the setup).
    CVarSetInteger(OcarinaStudio::Cvar::StageMode, 1);
    CVarSetInteger(OcarinaStudio::Cvar::HideHud, 1);
    CVarSetInteger(OcarinaStudio::Cvar::HideMessages, 1);
    if (CVarGetInteger("gOcarinaStudio.SetupVersion", 0) < 2) {
        CVarSetInteger(CVAR_SETTING("InterpolationFPS"), 30);
        CVarSetInteger("gOcarinaStudio.SetupVersion", 2);
        CVarSave();
    }

    // Same set-up the game's own "boot to warp point" uses (see Enhancements/Warping.cpp).
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.fileNum = 0xFE; // temporary file so the debug save is used
    Sram_InitDebugSave();
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.magic = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.magicLevel = gSaveContext.magic;
    gSaveContext.fileNum = 0xFF;
    gSaveContext.sceneLayer = 0;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.linkAge = 1; // child Link by default
    gSaveContext.nightFlag = 0;
    gSaveContext.skyboxTime = gSaveContext.dayTime = 0x8000;

    for (int i = 0; i < ARRAY_COUNT(gSaveContext.buttonStatus); i++) {
        gSaveContext.buttonStatus[i] = BTN_ENABLED;
    }
    gSaveContext.nextHudVisibilityMode = gSaveContext.hudVisibilityMode = gSaveContext.hudVisibilityModeTimer = 0;
    gSaveContext.forceRisingButtonAlphas = 0;
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    gSaveContext.entranceIndex = kStartEntrance;
    gSaveContext.seqId = static_cast<u8>(NA_BGM_DISABLED);
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = false;
    gWeatherMode = 0;

    title->state.running = false;
    SET_NEXT_GAMESTATE(&title->state, Play_Init, PlayState);
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
}

void RegisterBoot() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnZTitleUpdate>(BootToStage);
}

RegisterShipInitFunc sBootInit(RegisterBoot);

} // namespace
