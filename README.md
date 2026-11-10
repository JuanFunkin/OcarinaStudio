# OcarinaStudio

A machinima tool for **The Legend of Zelda: Ocarina of Time**, inspired by Saturn 64 (the Super Mario 64 machinima tool) and built on top of the [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) PC port source code.

The goal is simple: give machinima creators the same kind of workflow Saturn 64 offers for SM64, adapted to Ocarina of Time. A clean HUD-free view, a green-screen mode, a free camera, character customization and a pose/animation editor.

> **Status:** early planning / pre-alpha. Nothing here is playable yet. This README describes the intended feature set and roadmap.

---

## Planned features

### Character customization
- **Color palettes:** change the colors of Link's tunic, cap, skin, hair, boots, gloves, belt and more, with a live preview.
- **Save and load palettes:** store your own color presets and reuse them later.
- **Model swapping:** swap Link's model for custom ones. Because Ship of Harkinian is a PC port, it already supports loading custom models and textures, and OcarinaStudio builds on that.
- **Eye changing:** pick from the available eye textures.
- **Custom eyes:** drop your own eye textures into a folder and they show up in the menu, the same way Saturn 64 handles custom eyes.

### Machinima view
- **Green-screen mode:** Link is rendered on a solid green background (chroma key) so it is easy to composite in any video editor.
- **No HUD:** the in-game HUD is hidden so nothing covers the frame.
- **Game mode selection:** choose whether to go into a world (a scene) or stay in the green-screen stage.

### Camera
- **Freeze camera:** freeze the current camera.
- **Free camera:** move and rotate the camera freely while frozen. This is a dedicated machinima camera, not the stock Ocarina of Time camera, so nothing in the way of your shot.

### Pose and animation editor
- A section to build **poses and animations for Link**, similar to Saturn Studio.
- Keyframe-based posing and playback.

---

## Roadmap

1. **Foundation:** repository setup, Ship of Harkinian integration, manual GitHub Actions build.
2. **Machinima mode:** HUD removal, green-screen stage, scene selection.
3. **Camera:** freeze camera and free camera controls.
4. **Customization:** palette editor with save/load, eye selection, custom eyes folder, model swapping.
5. **Animation:** pose editor, then keyframe animation.
6. **Polish:** menus, input mapping, documentation and release builds.

---

## Requirements

OcarinaStudio does **not** include any Nintendo game data. You need to provide your own legally obtained copy of the Ocarina of Time ROM, exactly as Ship of Harkinian requires.

---

## Credits

- [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by HarbourMasters, the PC port this project is built on.
- Saturn 64, the inspiration for the feature set and workflow.

OcarinaStudio is a fan project and is not affiliated with or endorsed by Nintendo, HarbourMasters or the Saturn 64 team.

---

## License

The license will be defined once the Ship of Harkinian integration is in place, so that it respects the licenses of the upstream code.
