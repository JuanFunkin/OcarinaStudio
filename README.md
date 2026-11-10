<p align="center">
  <img src="assets/banner.png" alt="Ocarina Studio" width="640">
</p>

A machinima tool for **The Legend of Zelda: Ocarina of Time**, inspired by Saturn 64 (the Super Mario 64 machinima tool) and built on top of the [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) PC port source code.

The goal is to give machinima creators the same kind of workflow Saturn 64 offers for SM64, adapted to Ocarina of Time: a clean view with no HUD, a green screen, a free camera, character customization and a pose/animation editor.

> **Status: pre-alpha.** All the features below are written, but the project has **not been compiled or played yet**. The first manual build on GitHub Actions is the first real test, so expect compile errors and bugs while that gets worked out.

---

## Features

Everything lives in one panel, opened with **F10** (or from the window list). It has seven tabs.

| Tab | What it does |
| --- | --- |
| **Stage** | Hide the HUD and message boxes. Green screen stage: hides the scene and sky and fills the background with a flat color (green by default, any color you want) so Link can be keyed out in your video editor. One click on *Start clean stage* warps to an empty room with everything hidden. |
| **Worlds** | Warp to a world (scene), or to any entrance index. Reload the current scene. |
| **Camera** | **Game / Frozen / Free camera.** The free camera is OcarinaStudio's own, not the stock one, so nothing covers the shot. Adjustable speed, look sensitivity, roll and field of view. |
| **Link** | Change the colors of Link's tunics, hair, linen and boots. **Save and load palettes** as small `.json` files. |
| **Eyes** | Force an eye expression and mouth. **Custom eyes:** drop PNG files in a folder and assign them to any eye slot, for adult and/or child Link. |
| **Models** | Enable and disable model packs (`.o2r`) to swap Link's model, using Ship of Harkinian's own mod support. |
| **Pose** | Pose Link limb by limb, save and load poses, and build **keyframe animations** with playback, looping and speed control. |

### Free camera controls

| Input | Action |
| --- | --- |
| `I` / `K` | Forward / back |
| `J` / `L` | Strafe left / right |
| `U` / `O` | Down / up |
| Right mouse button + drag | Look around |
| `Shift` / `Ctrl` | Faster / slower |

### Your data folders

OcarinaStudio keeps everything in an `OcarinaStudio` folder next to Ship of Harkinian's save data. The folders are created automatically.

| Folder | Contents |
| --- | --- |
| `OcarinaStudio/eyes/` | Your custom eye images (`.png`). They are resized to the original eye texture size. |
| `OcarinaStudio/palettes/` | Saved color palettes (`.json`). |
| `OcarinaStudio/models/` | Model packs (`.o2r`). |
| `OcarinaStudio/poses/` | Saved poses (`.json`). |
| `OcarinaStudio/animations/` | Saved keyframe animations (`.json`). |

### Known limitations

- **Windows and Linux only.** There is no macOS build yet.
- The free camera replaces the game camera's *output*; the game camera keeps running underneath, so cutscenes and targeting still behave normally.
- The pose editor takes over limbs on top of the game's animation. *Lock Link in place* stops his movement while a pose or animation is active.
- Switching model packs reloads the current scene so Link is rebuilt with the new model.

---

## Roadmap

- [x] Machinima stage: no HUD, green screen, world selection
- [x] Freeze camera and free camera
- [x] Link colors with saved palettes
- [x] Eye expressions and custom eyes folder
- [x] Model pack switching
- [x] Pose editor and keyframe animation
- [ ] First successful build and in-game testing
- [ ] macOS build
- [ ] Camera keyframes and path playback
- [ ] Video / image-sequence capture

---

## Project layout

OcarinaStudio does not fork Ship of Harkinian's source. It is applied on top of it:

```
patches/    The few engine hooks: HUD, background, camera output, window registration
overlay/    The OcarinaStudio module, added to Ship of Harkinian as new files
assets/     Logo and application icons
scripts/    apply.sh, which applies the patches, the module and the icons
```

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

The license will be defined once the Ship of Harkinian integration is confirmed, so that it respects the licenses of the upstream code.
