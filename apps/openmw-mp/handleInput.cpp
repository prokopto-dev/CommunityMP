#include "ConsoleInput.hpp"
#include "ServerEventDispatcher.hpp"

#include <iostream>
#include <string>

namespace mwmp_input {
    std::string windowInputBuffer;
    void handler() {
        while (mwmp::ConsoleInput::hasInput()) {
            const int input = mwmp::ConsoleInput::readChar();
            if (input < 0)
                break;

            const char c = static_cast<char>(input);
            std::cout << c << std::flush;
            if (mwmp::ConsoleInput::isEnter(c)) {
                std::cout << std::endl;
                mwmp::ServerEvents::serverWindowInput(windowInputBuffer.c_str());
                windowInputBuffer.assign("");
            }
            else if (c == '\b') {
                auto size = windowInputBuffer.size();
                if (size > 0)
                    windowInputBuffer.erase(size - 1);
            }
            else windowInputBuffer += c;
        }
    }
}
