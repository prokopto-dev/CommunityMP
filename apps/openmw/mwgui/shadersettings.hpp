#ifndef MWGUI_SHADERSETTINGS_H
#define MWGUI_SHADERSETTINGS_H

namespace MWGui
{
    // ImGui pane that surfaces the [Shaders] settings normally
    // edited via settings.cfg only — auto-load patterns, parallax
    // toggle, etc. Toggling any value triggers a shader reload so
    // the change is immediate.
    class ShaderSettings
    {
    public:
        void draw();
    };
}

#endif
