#ifndef OPENMW_MP_CONSOLEINPUT_HPP
#define OPENMW_MP_CONSOLEINPUT_HPP

namespace mwmp::ConsoleInput
{
    bool hasInput();
    int readChar();
    bool isEnter(int character);
    bool consumeEnterPress();
}

#endif
