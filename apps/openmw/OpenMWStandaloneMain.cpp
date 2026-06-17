#include <components/debug/debugging.hpp>

#include "OpenMWApplication.hpp"

#if defined(_WIN32)
#include <components/misc/windows.hpp>
#include <cstdlib>
#endif

#ifdef ANDROID
extern "C" int SDL_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    return Debug::wrapApplication(&runApplication, argc, argv, "OpenMW");
}

// Platform specific for Windows when there is no console built into the executable.
// Windows will call the WinMain function instead of main in this case, the normal
// main function is then called with the __argc and __argv parameters.
#if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}
#endif
