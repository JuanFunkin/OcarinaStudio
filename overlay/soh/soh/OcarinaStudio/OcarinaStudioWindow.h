#pragma once

#include <ship/window/gui/GuiWindow.h>

// Ocarina Studio's main interface: a menu bar across the top (like Saturn 64) plus one floating tool
// window per feature. It draws no window of its own. F10 shows / hides all of it for a clean capture.
class OcarinaStudioWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
    void Draw() override;
};
