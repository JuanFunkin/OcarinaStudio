#include "soh/OcarinaStudio/OSInternal.h"

#include <algorithm>
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

/*
 * On-screen controller for phones and tablets.
 *
 * The overlay draws the buttons and reads the fingers directly from SDL (so several can be used at once), and
 * feeds a virtual SDL gamepad. The game sees it like any other controller, so the normal input settings apply.
 * Every control can be moved and resized in the "Touch controls" window (Edit layout).
 *
 * It is on by default on Android. On other systems set OCARINA_TOUCH=1 to try it.
 */

namespace {

enum Kind { kStick, kButton, kTrigger, kTriggerR, kShoulder, kStart };

struct Control {
    const char* id;       // used in the saved settings
    const char* label;
    Kind kind;
    float x, y;           // default center, fraction of the screen
    float r;              // default radius, fraction of the screen height
    int sdlButton;        // SDL_CONTROLLER_BUTTON_* (buttons / shoulders / start)
    int axis;             // right stick axis for the C buttons: 0 none, 1 up, 2 down, 3 left, 4 right
    ImU32 color;
};

// The default mapping of the game's controller settings (ControllerDefaultMappings): A = A, B = B, L = left
// shoulder, Z = left trigger, R = right trigger, C buttons = right stick, Start = start.
Control sControls[] = {
    { "stick", "", kStick, 0.15f, 0.70f, 0.17f, -1, 0, IM_COL32(220, 220, 230, 255) },
    { "a", "A", kButton, 0.90f, 0.76f, 0.085f, SDL_CONTROLLER_BUTTON_A, 0, IM_COL32(80, 200, 120, 255) },
    { "b", "B", kButton, 0.80f, 0.86f, 0.07f, SDL_CONTROLLER_BUTTON_B, 0, IM_COL32(230, 90, 80, 255) },
    { "cup", "C^", kButton, 0.86f, 0.28f, 0.055f, -1, 1, IM_COL32(240, 200, 60, 255) },
    { "cdown", "Cv", kButton, 0.86f, 0.50f, 0.055f, -1, 2, IM_COL32(240, 200, 60, 255) },
    { "cleft", "C<", kButton, 0.78f, 0.39f, 0.055f, -1, 3, IM_COL32(240, 200, 60, 255) },
    { "cright", "C>", kButton, 0.94f, 0.39f, 0.055f, -1, 4, IM_COL32(240, 200, 60, 255) },
    { "z", "Z", kTrigger, 0.08f, 0.36f, 0.065f, -1, 0, IM_COL32(170, 170, 200, 255) },
    { "l", "L", kShoulder, 0.08f, 0.20f, 0.055f, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0, IM_COL32(170, 170, 200, 255) },
    { "r", "R", kTriggerR, 0.92f, 0.12f, 0.06f, -1, 0, IM_COL32(170, 170, 200, 255) },
    { "start", "START", kStart, 0.50f, 0.92f, 0.05f, SDL_CONTROLLER_BUTTON_START, 0, IM_COL32(200, 200, 200, 255) },
};
constexpr int kControlCount = sizeof(sControls) / sizeof(sControls[0]);

std::mutex sFingerMutex;
std::map<SDL_FingerID, ImVec2> sFingers; // normalized 0..1
SDL_Joystick* sPad = nullptr;
bool sPadTried = false;
bool sWatching = false;
bool sOpen = false;     // the settings window
bool sEdit = false;
int sDragging = -1;
SDL_FingerID sStickFinger = -1;
std::set<SDL_FingerID> sSeen;    // fingers already looked at
std::set<SDL_FingerID> sBlocked; // fingers that landed on a menu: they never press game controls
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

void EnsurePad() {
    if (!sWatching) {
        sWatching = true;
        SDL_AddEventWatch(EventWatch, nullptr);
    }
    if (sPad != nullptr || sPadTried) {
        return;
    }
    sPadTried = true;
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "OcarinaStudio Touch Controls";
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    if (index >= 0) {
        sPad = SDL_JoystickOpen(index);
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

int16_t ToAxis(float v) {
    return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
}

} // namespace

namespace OcarinaStudio {

bool TouchControlsEnabled() {
    return Enabled();
}

// Every frame: reads the fingers, updates the virtual gamepad and draws the controls.
void DrawTouchOverlay(bool hide) {
    if (!Enabled()) {
        return;
    }
    EnsurePad();

    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float alpha = Opacity();
    ImGuiIO& io = ImGui::GetIO();

    // The SoH menu is open: the on-screen controller steps aside so taps on the menu never press game buttons.
    const bool menuOpen = CVarGetInteger(CVAR_WINDOW("Menu"), 0) != 0;
    hide = hide || (menuOpen && !sEdit);

    std::vector<ImVec2> fingers;     // pixels, fingers that may press controls
    std::vector<ImVec2> allFingers;  // pixels, every finger
    {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        // A finger that lands on a menu or window belongs to that window for as long as it stays down.
        for (auto it = sSeen.begin(); it != sSeen.end();) {
            it = sFingers.count(*it) != 0 ? std::next(it) : sSeen.erase(it);
        }
        for (auto it = sBlocked.begin(); it != sBlocked.end();) {
            it = sFingers.count(*it) != 0 ? std::next(it) : sBlocked.erase(it);
        }
        for (const auto& entry : sFingers) {
            if (sSeen.insert(entry.first).second && io.WantCaptureMouse && !sEdit) {
                sBlocked.insert(entry.first);
            }
            allFingers.emplace_back(entry.second.x * screen.x, entry.second.y * screen.y);
            if (sBlocked.count(entry.first) == 0) {
                fingers.emplace_back(entry.second.x * screen.x, entry.second.y * screen.y);
            }
        }
        if (sStickActive && sBlocked.count(sStickFinger) != 0) {
            sStickActive = false;
        }
    }

    // Two fingers dragging over a menu scroll it (a single finger is a mouse: it presses and drags widgets).
    if (allFingers.size() >= 2 && io.WantCaptureMouse) {
        const float avgY = (allFingers[0].y + allFingers[1].y) * 0.5f;
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
    bool triggerZ = false;
    bool triggerR = false;
    bool buttons[SDL_CONTROLLER_BUTTON_MAX] = {};

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
                        if (sBlocked.count(entry.first) != 0) {
                            continue;
                        }
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

        if (pressed && c.kind != kStick) {
            if (c.kind == kTrigger) {
                triggerZ = true;
            } else if (c.kind == kTriggerR) {
                triggerR = true;
            } else if (c.axis == 1) {
                cY = -1.0f;
            } else if (c.axis == 2) {
                cY = 1.0f;
            } else if (c.axis == 3) {
                cX = -1.0f;
            } else if (c.axis == 4) {
                cX = 1.0f;
            } else if (c.sdlButton >= 0) {
                buttons[c.sdlButton] = true;
            }
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

    if (sPad != nullptr) {
        SDL_JoystickSetVirtualAxis(sPad, SDL_CONTROLLER_AXIS_LEFTX, ToAxis(stickX));
        SDL_JoystickSetVirtualAxis(sPad, SDL_CONTROLLER_AXIS_LEFTY, ToAxis(stickY));
        SDL_JoystickSetVirtualAxis(sPad, SDL_CONTROLLER_AXIS_RIGHTX, ToAxis(cX));
        SDL_JoystickSetVirtualAxis(sPad, SDL_CONTROLLER_AXIS_RIGHTY, ToAxis(cY));
        SDL_JoystickSetVirtualAxis(sPad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, triggerZ ? 32767 : -32768);
        SDL_JoystickSetVirtualAxis(sPad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, triggerR ? 32767 : -32768);
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            SDL_JoystickSetVirtualButton(sPad, b, buttons[b] ? 1 : 0);
        }
    }
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
