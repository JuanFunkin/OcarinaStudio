#include "soh/OcarinaStudio/OSInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/cvar_prefixes.h"
#include "soh/OcarinaStudio/OcarinaStudio.h"

extern "C" {
#include <libultraship/libultra/controller.h>
}

/*
 * On-screen controller for phones and tablets.
 *
 * The overlay draws the buttons and reads the fingers directly from SDL (so several can be used at once). The
 * result is written straight into the game's controller data (OcarinaStudio_InjectTouchPad, called by padmgr.c
 * every frame), so it works without any gamepad being detected.
 * Every control can be moved and resized in the "Touch controls" window (Edit layout).
 *
 * It is on by default on Android. On other systems set OCARINA_TOUCH=1 to try it.
 */

namespace {

enum Kind { kStick, kButton };

// N64 controller bits as the game reads them (same values as A_BUTTON, B_BUTTON, ... in libultra).
constexpr uint16_t kBtnA = 0x8000, kBtnB = 0x4000, kBtnZ = 0x2000, kBtnStart = 0x1000, kBtnL = 0x0020,
                   kBtnR = 0x0010, kBtnCUp = 0x0008, kBtnCDown = 0x0004, kBtnCLeft = 0x0002, kBtnCRight = 0x0001;

struct Control {
    const char* id;       // used in the saved settings
    const char* label;
    Kind kind;
    float x, y;           // default center, fraction of the screen
    float r;              // default radius, fraction of the screen height
    uint16_t bit;         // N64 button bit (buttons only)
    ImU32 color;
};

Control sControls[] = {
    { "stick", "", kStick, 0.15f, 0.70f, 0.17f, 0, IM_COL32(220, 220, 230, 255) },
    { "a", "A", kButton, 0.90f, 0.76f, 0.085f, kBtnA, IM_COL32(80, 200, 120, 255) },
    { "b", "B", kButton, 0.80f, 0.86f, 0.07f, kBtnB, IM_COL32(230, 90, 80, 255) },
    { "cup", "C^", kButton, 0.86f, 0.28f, 0.055f, kBtnCUp, IM_COL32(240, 200, 60, 255) },
    { "cdown", "Cv", kButton, 0.86f, 0.50f, 0.055f, kBtnCDown, IM_COL32(240, 200, 60, 255) },
    { "cleft", "C<", kButton, 0.78f, 0.39f, 0.055f, kBtnCLeft, IM_COL32(240, 200, 60, 255) },
    { "cright", "C>", kButton, 0.94f, 0.39f, 0.055f, kBtnCRight, IM_COL32(240, 200, 60, 255) },
    { "z", "Z", kButton, 0.08f, 0.36f, 0.065f, kBtnZ, IM_COL32(170, 170, 200, 255) },
    { "l", "L", kButton, 0.08f, 0.20f, 0.055f, kBtnL, IM_COL32(170, 170, 200, 255) },
    { "r", "R", kButton, 0.92f, 0.12f, 0.06f, kBtnR, IM_COL32(170, 170, 200, 255) },
    { "start", "START", kButton, 0.50f, 0.92f, 0.05f, kBtnStart, IM_COL32(200, 200, 200, 255) },
};
constexpr int kControlCount = sizeof(sControls) / sizeof(sControls[0]);

std::mutex sFingerMutex;
std::map<SDL_FingerID, ImVec2> sFingers; // normalized 0..1
// What the game reads this frame (written by the overlay, read by padmgr.c).
std::atomic<uint16_t> sPadButtons{ 0 };
std::atomic<int> sPadStickX{ 0 };
std::atomic<int> sPadStickY{ 0 };
bool sWatching = false;
bool sOpen = false;     // the settings window
bool sEdit = false;
int sDragging = -1;
SDL_FingerID sStickFinger = -1;
float sScrollPrevY = -1.0f;
bool sStickActive = false;

std::string Key(const Control& c, const char* field) {
    return std::string("gOcarinaStudio.Touch.") + c.id + "." + field;
}

float PosX(const Control& c) { return CVarGetFloat(Key(c, "X").c_str(), c.x); }
float PosY(const Control& c) { return CVarGetFloat(Key(c, "Y").c_str(), c.y); }

float Scale() { return CVarGetFloat("gOcarinaStudio.Touch.Scale", 1.0f); }
float Opacity() { return CVarGetFloat("gOcarinaStudio.Touch.Opacity", 0.55f); }

bool Enabled() {
#ifdef __ANDROID__
    return CVarGetInteger("gOcarinaStudio.Touch.Enabled", 1) != 0;
#else
    return std::getenv("OCARINA_TOUCH") != nullptr && CVarGetInteger("gOcarinaStudio.Touch.Enabled", 1) != 0;
#endif
}

int EventWatch(void*, SDL_Event* event) {
    if (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERMOTION) {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        sFingers[event->tfinger.fingerId] = ImVec2(event->tfinger.x, event->tfinger.y);
    } else if (event->type == SDL_FINGERUP) {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        sFingers.erase(event->tfinger.fingerId);
    }
    return 0;
}

void EnsureWatch() {
    if (!sWatching) {
        sWatching = true;
        SDL_AddEventWatch(EventWatch, nullptr);
    }
}

// Screen geometry of a control, in pixels.
struct Geo {
    ImVec2 center;
    float radius;
};

Geo Place(const Control& c, ImVec2 screen) {
    return { ImVec2(PosX(c) * screen.x, PosY(c) * screen.y), CVarGetFloat(Key(c, "R").c_str(), c.r) * Scale() * screen.y };
}

float Dist(ImVec2 a, ImVec2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

} // namespace

namespace OcarinaStudio {

bool TouchControlsEnabled() {
    return Enabled();
}

// The settings window only exists on Android (PC has no touch screen to configure).
bool TouchControlsAvailable() {
#ifdef __ANDROID__
    return true;
#else
    return std::getenv("OCARINA_TOUCH") != nullptr;
#endif
}

// Every frame: reads the fingers, updates the virtual gamepad and draws the controls.
void DrawTouchOverlay(bool hide) {
    if (!Enabled()) {
        return;
    }
    EnsureWatch();

    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float alpha = Opacity();
    ImGuiIO& io = ImGui::GetIO();

    // The SoH menu is open: the on-screen controller steps aside so taps on the menu never press game buttons.
    const bool menuOpen = SohMenuVisible();
    hide = hide || (menuOpen && !sEdit);

    std::vector<ImVec2> fingers; // pixels
    {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        for (const auto& entry : sFingers) {
            fingers.emplace_back(entry.second.x * screen.x, entry.second.y * screen.y);
        }
    }

    // SoH menu open: two fingers dragging scroll it (one finger is the mouse: it presses and drags widgets).
    if (menuOpen && fingers.size() >= 2) {
        const float avgY = (fingers[0].y + fingers[1].y) * 0.5f;
        if (sScrollPrevY >= 0.0f) {
            io.AddMouseWheelEvent(0.0f, (avgY - sScrollPrevY) / (5.0f * ImGui::GetFontSize()));
        }
        sScrollPrevY = avgY;
    } else {
        sScrollPrevY = -1.0f;
    }

    // Layout editing: drag controls with the mouse / one finger (SDL turns touches into mouse events).
    if (sEdit) {
        if (ImGui::IsMouseClicked(0)) {
            sDragging = -1;
            for (int i = 0; i < kControlCount; i++) {
                const Geo g = Place(sControls[i], screen);
                if (Dist(io.MousePos, g.center) <= g.radius) {
                    sDragging = i;
                }
            }
        }
        if (sDragging >= 0 && ImGui::IsMouseDown(0)) {
            CVarSetFloat(Key(sControls[sDragging], "X").c_str(), std::clamp(io.MousePos.x / screen.x, 0.0f, 1.0f));
            CVarSetFloat(Key(sControls[sDragging], "Y").c_str(), std::clamp(io.MousePos.y / screen.y, 0.0f, 1.0f));
        } else if (sDragging >= 0) {
            sDragging = -1;
            CVarSave();
        }
    }

    float stickX = 0.0f, stickY = 0.0f;
    float cX = 0.0f, cY = 0.0f;
    uint16_t bits = 0;

    for (int i = 0; i < kControlCount; i++) {
        const Control& c = sControls[i];
        const Geo g = Place(c, screen);
        bool pressed = false;

        if (!sEdit && !hide) {
            if (c.kind == kStick) {
                // The finger that lands inside the stick area keeps the stick until it is lifted.
                std::lock_guard<std::mutex> lock(sFingerMutex);
                if (sStickActive && sFingers.find(sStickFinger) == sFingers.end()) {
                    sStickActive = false;
                }
                if (!sStickActive) {
                    for (const auto& entry : sFingers) {
                        const ImVec2 p(entry.second.x * screen.x, entry.second.y * screen.y);
                        if (Dist(p, g.center) <= g.radius * 1.5f) {
                            sStickActive = true;
                            sStickFinger = entry.first;
                            break;
                        }
                    }
                }
                if (sStickActive) {
                    const ImVec2 f = sFingers[sStickFinger];
                    const ImVec2 p(f.x * screen.x, f.y * screen.y);
                    stickX = std::clamp((p.x - g.center.x) / g.radius, -1.0f, 1.0f);
                    stickY = std::clamp((p.y - g.center.y) / g.radius, -1.0f, 1.0f);
                    pressed = true;
                }
            } else {
                for (const ImVec2& f : fingers) {
                    if (Dist(f, g.center) <= g.radius * 1.15f) {
                        pressed = true;
                    }
                }
            }
        }

        if (pressed && c.kind == kButton) {
            bits |= c.bit;
        }

        if (hide && !sEdit) {
            continue;
        }
        const int a = static_cast<int>(255.0f * alpha * (pressed ? 1.0f : 0.8f));
        const ImU32 base = (c.color & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24);
        if (c.kind == kStick) {
            draw->AddCircle(g.center, g.radius, base, 48, 3.0f);
            ImVec2 knob = g.center;
            if (sStickActive) {
                knob = ImVec2(g.center.x + stickX * g.radius, g.center.y + stickY * g.radius);
            }
            draw->AddCircleFilled(knob, g.radius * 0.42f, base, 32);
        } else {
            draw->AddCircleFilled(g.center, g.radius, pressed ? base : (base & 0x60FFFFFF), 32);
            draw->AddCircle(g.center, g.radius, base, 32, 2.5f);
            const ImVec2 size = ImGui::CalcTextSize(c.label);
            draw->AddText(ImVec2(g.center.x - size.x * 0.5f, g.center.y - size.y * 0.5f), IM_COL32(0, 0, 0, 230),
                          c.label);
        }
    }

    // The stick: a small dead zone, then the full N64 range (about +-85).
    const float mag = std::sqrt(stickX * stickX + stickY * stickY);
    int sx = 0, sy = 0;
    if (mag > 0.12f) {
        const float scale = (mag > 1.0f ? 1.0f / mag : 1.0f) * 85.0f;
        sx = static_cast<int>(std::lround(stickX * scale));
        sy = static_cast<int>(std::lround(-stickY * scale)); // screen y grows downwards
    }
    sPadButtons = bits;
    sPadStickX = sx;
    sPadStickY = sy;
}

void DrawTouchTab() {
    bool enabled = Enabled();
    if (ImGui::Checkbox("Show touch controls", &enabled)) {
        CVarSetInteger("gOcarinaStudio.Touch.Enabled", enabled ? 1 : 0);
        CVarSave();
    }
    float opacity = Opacity();
    if (ImGui::SliderFloat("Opacity", &opacity, 0.1f, 1.0f, "%.2f")) {
        CVarSetFloat("gOcarinaStudio.Touch.Opacity", opacity);
        CVarSave();
    }
    float scale = Scale();
    if (ImGui::SliderFloat("Size (all)", &scale, 0.5f, 2.0f, "%.2fx")) {
        CVarSetFloat("gOcarinaStudio.Touch.Scale", scale);
        CVarSave();
    }
    float ui = CVarGetFloat("gOcarinaStudio.UiScale", 0.0f);
    if (ui <= 0.0f) {
        ui = ImGui::GetIO().FontGlobalScale;
    }
    if (ImGui::SliderFloat("Menu size", &ui, 0.8f, 3.0f, "%.2fx")) {
        CVarSetFloat("gOcarinaStudio.UiScale", ui);
        ImGui::GetIO().FontGlobalScale = ui;
        CVarSave();
    }

    ImGui::SeparatorText("Layout");
    ImGui::Checkbox("Edit layout (drag the buttons)", &sEdit);
    if (sEdit) {
        ImGui::TextWrapped("Drag each button to where you want it. Close this window or untick the box when done.");
        static int sSelected = 1;
        for (int i = 0; i < kControlCount; i++) {
            if (i > 0) {
                ImGui::SameLine();
            }
            const bool picked = i == sSelected;
            std::string label = std::string(sControls[i].kind == kStick ? "Stick" : sControls[i].label) + "##sel" + std::to_string(i);
            if (ImGui::RadioButton(label.c_str(), picked)) {
                sSelected = i;
            }
            if (i == 3 || i == 7) {
                ImGui::NewLine();
            }
        }
        Control& c = sControls[sSelected];
        float r = CVarGetFloat(Key(c, "R").c_str(), c.r);
        if (ImGui::SliderFloat("Size of the selected", &r, 0.03f, 0.25f, "%.3f")) {
            CVarSetFloat(Key(c, "R").c_str(), r);
            CVarSave();
        }
    }
    if (ImGui::Button("Reset layout")) {
        for (const Control& c : sControls) {
            CVarClear(Key(c, "X").c_str());
            CVarClear(Key(c, "Y").c_str());
            CVarClear(Key(c, "R").c_str());
        }
        CVarSetFloat("gOcarinaStudio.Touch.Scale", 1.0f);
        CVarSave();
    }
    ImGui::TextDisabled("Controls: stick = move, A / B, C buttons (right stick), Z, L, R, Start.");
    ImGui::TextDisabled("Change what each one does in the game's controller settings.");
}

} // namespace OcarinaStudio

// Called by padmgr.c right after the game read the controllers: adds what the fingers are doing.
extern "C" void OcarinaStudio_InjectTouchPad(void* data) {
    OSContPad* pad = static_cast<OSContPad*>(data);
    const uint16_t bits = sPadButtons.load();
    const int sx = sPadStickX.load();
    const int sy = sPadStickY.load();
    if (pad == nullptr || (bits == 0 && sx == 0 && sy == 0)) {
        return;
    }
    pad->button |= bits;
    if (sx != 0 || sy != 0) {
        pad->stick_x = static_cast<int8_t>(sx);
        pad->stick_y = static_cast<int8_t>(sy);
    }
}
