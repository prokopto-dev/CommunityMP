#include <chrono>
#include <exception>
#include <thread>
#include <components/openmw-mp/Transport/GnsTransport.hpp>

#include "Utils.hpp"

unsigned int PingServer(const char *addr, unsigned short port)
{
    try
    {
        mwmp::GnsTransport transport(mwmp::GnsMode::Client, false);
        const auto start = std::chrono::steady_clock::now();
        transport.connect(addr, port);

        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(PING_UNREACHABLE))
        {
            for (mwmp::ReceivedPacket* receivedPacket = transport.receive(); receivedPacket;
                 transport.deallocatePacket(receivedPacket), receivedPacket = transport.receive())
            {
                switch (receivedPacket->id())
                {
                    case ID_CONNECTION_REQUEST_ACCEPTED:
                    {
                        const auto now = std::chrono::steady_clock::now();
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                        return elapsed > PING_UNREACHABLE ? PING_UNREACHABLE : static_cast<unsigned int>(elapsed);
                    }
                    case ID_CONNECTION_ATTEMPT_FAILED:
                    case ID_CONNECTION_LOST:
                    case ID_DISCONNECTION_NOTIFICATION:
                        return PING_UNREACHABLE;
                    default:
                        break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    catch (const std::exception&)
    {
    }

    return PING_UNREACHABLE;
}
