#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "ScriptController.hpp"

unsigned char ScriptController::getPacketOriginFromContextType(unsigned short contextType)
{
    switch (contextType)
    {
        case ScriptController::Console:
            return static_cast<unsigned char>(mwmp::CLIENT_CONSOLE);
        case ScriptController::Dialogue:
            return static_cast<unsigned char>(mwmp::CLIENT_DIALOGUE);
        case ScriptController::ScriptLocal:
            return static_cast<unsigned char>(mwmp::CLIENT_SCRIPT_LOCAL);
        case ScriptController::ScriptGlobal:
            return static_cast<unsigned char>(mwmp::CLIENT_SCRIPT_GLOBAL);
    }

    return static_cast<unsigned char>(mwmp::CLIENT_GAMEPLAY);
}

