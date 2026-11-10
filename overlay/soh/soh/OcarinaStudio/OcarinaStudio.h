#ifndef OCARINA_STUDIO_H
#define OCARINA_STUDIO_H

/*
 * OcarinaStudio - C bridge.
 *
 * These functions are called from the (patched) C engine code of Ship of Harkinian
 * (z_play.c, z_camera.c). Keep this header plain C so it can be included from both languages.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

/* Machinima toggles. Return non-zero when the feature is active. */
int32_t OcarinaStudio_HideHud(void);
int32_t OcarinaStudio_HideMessages(void);
int32_t OcarinaStudio_HideWorld(void);

/* Frame clear color. Black normally, the chroma-key color while the stage (green screen) is on. */
void OcarinaStudio_GetClearColor(uint8_t* r, uint8_t* g, uint8_t* b);

/*
 * Called right before the active camera is applied to the view. May overwrite eye/at/up (3 floats
 * each) and the vertical fov to freeze the camera or to drive the free camera.
 */
void OcarinaStudio_OverrideView(struct PlayState* play, float* eye, float* at, float* up, float* fovy);

#ifdef __cplusplus
}
#endif

#endif /* OCARINA_STUDIO_H */
