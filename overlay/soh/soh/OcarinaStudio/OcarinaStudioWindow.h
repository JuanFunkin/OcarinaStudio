#pragma once

#include <ship/window/gui/GuiWindow.h>

// The main Ocarina Studio panel. Toggle it with F10 or from the window list.
class OcarinaStudioWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
};
