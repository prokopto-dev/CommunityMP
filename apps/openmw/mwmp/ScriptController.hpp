#ifndef OPENMW_SCRIPTCONTROLLER_HPP
#define OPENMW_SCRIPTCONTROLLER_HPP

namespace ScriptController
{
    enum ContextType : unsigned short
    {
        Unknown = 65535,
        Console = 0,
        Dialogue = 1,
        ScriptLocal = 2,
        ScriptGlobal = 3
    };

    unsigned char getPacketOriginFromContextType(unsigned short contextType);
}


#endif //OPENMW_SCRIPTCONTROLLER_HPP

