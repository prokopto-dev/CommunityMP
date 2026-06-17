#include "ConsoleInput.hpp"

#ifdef _WIN32
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

namespace mwmp::ConsoleInput
{
    bool hasInput()
    {
#ifdef _WIN32
        return _kbhit() != 0;
#else
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(STDIN_FILENO, &readSet);

        timeval timeout {};
        return select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &timeout) > 0 && FD_ISSET(STDIN_FILENO, &readSet);
#endif
    }

    int readChar()
    {
#ifdef _WIN32
        return _getch();
#else
        unsigned char character = 0;
        return read(STDIN_FILENO, &character, 1) == 1 ? character : -1;
#endif
    }

    bool isEnter(int character)
    {
        return character == '\n' || character == '\r';
    }

    bool consumeEnterPress()
    {
        while (hasInput())
        {
            const int character = readChar();
            if (character < 0)
                return false;

            if (isEnter(character))
                return true;
        }

        return false;
    }
}
