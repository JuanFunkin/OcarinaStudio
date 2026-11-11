#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

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

namespace {

constexpr int kStartEntrance = 0x0BB; // Link's House

void BootToStage(void* gameState) {
    if (CVarGetInteger("gOcarinaStudio.BootToStage", 1) == 0) {
        return;
    }
    auto* title = static_cast<TitleContext*>(gameState);

    // First boot only: show the clean stage (the user can change it afterwards).
    if (CVarGetInteger("gOcarinaStudio.Configured", 0) == 0) {
        CVarSetInteger(OcarinaStudio::Cvar::StageMode, 1);
        CVarSetInteger(OcarinaStudio::Cvar::HideHud, 1);
        CVarSetInteger(OcarinaStudio::Cvar::HideMessages, 1);
        CVarSetInteger("gOcarinaStudio.Configured", 1);
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
    gSaveContext.linkAge = 0;
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
