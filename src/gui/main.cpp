#include "gui_application.h"

int main() {
  GuiApplication app;
  if (app.Init(1280, 720, "Raytracer Studio (ImGui)")) {
    app.Run();
  }
  return 0;
}
