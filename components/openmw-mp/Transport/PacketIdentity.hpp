#ifndef OPENMW_MP_PACKETIDENTITY_HPP
#define OPENMW_MP_PACKETIDENTITY_HPP

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace mwmp
{
    struct PacketAddress
    {
        std::string host;
        unsigned short port = 0;

        friend bool operator==(const PacketAddress& left, const PacketAddress& right)
        {
            return left.host == right.host && left.port == right.port;
        }

        friend bool operator!=(const PacketAddress& left, const PacketAddress& right)
        {
            return !(left == right);
        }

        friend bool operator<(const PacketAddress& left, const PacketAddress& right)
        {
            if (left.host != right.host)
                return left.host < right.host;

            return left.port < right.port;
        }
    };

    struct PacketNetworkId
    {
        std::uint64_t value = 0;
    };

    struct PacketGuid
    {
        std::uint64_t value = static_cast<std::uint64_t>(-1);

        friend constexpr bool operator==(PacketGuid left, PacketGuid right)
        {
            return left.value == right.value;
        }

        friend constexpr bool operator!=(PacketGuid left, PacketGuid right)
        {
            return !(left == right);
        }

        friend constexpr bool operator<(PacketGuid left, PacketGuid right)
        {
            return left.value < right.value;
        }
    };

    inline PacketGuid unassignedPacketGuid()
    {
        return PacketGuid{};
    }

    inline PacketAddress unassignedPacketAddress()
    {
        return PacketAddress{};
    }

    inline PacketAddress makePacketAddress(const char* host, unsigned short port)
    {
        std::string hostText = host != nullptr ? host : "";
        if (hostText.size() >= 2 && hostText.front() == '[' && hostText.back() == ']')
            hostText = hostText.substr(1, hostText.size() - 2);

        return PacketAddress{ hostText, port };
    }

    inline bool isPacketAddressNumericHost(const std::string& host)
    {
        if (host.empty())
            return true;

        std::string hostText = host;
        if (hostText.size() >= 2 && hostText.front() == '[' && hostText.back() == ']')
            hostText = hostText.substr(1, hostText.size() - 2);

        return std::all_of(hostText.begin(), hostText.end(), [](unsigned char c) {
            return std::isdigit(c) || c == '.' || c == ':' || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
    }

    inline unsigned short packetAddressPort(const PacketAddress& address)
    {
        return address.port;
    }

    inline void setPacketAddressPortHostOrder(PacketAddress& address, unsigned short port)
    {
        address.port = port;
    }

    inline std::size_t packetGuidSize()
    {
        return sizeof(std::uint64_t);
    }

    inline PacketGuid makePacketGuid(std::uint64_t value)
    {
        return PacketGuid{ value };
    }

    inline std::uint64_t packetGuidValue(PacketGuid guid)
    {
        return guid.value;
    }

    inline std::string packetGuidToString(PacketGuid guid)
    {
        if (guid == unassignedPacketGuid())
            return "UNASSIGNED_PACKET_GUID";

        return std::to_string(guid.value);
    }

    template <class Stream>
    inline void writePacketGuid(Stream& stream, PacketGuid guid)
    {
        stream.Write(packetGuidValue(guid));
    }

    template <class Stream>
    inline bool readPacketGuid(Stream& stream, PacketGuid& guid)
    {
        std::uint64_t value = 0;
        if (!stream.Read(value))
            return false;

        guid = makePacketGuid(value);
        return true;
    }

    inline std::string packetAddressToString(const PacketAddress& address, bool writePort)
    {
        if (!writePort)
            return address.host;

        if (address.host.find(':') != std::string::npos)
            return "[" + address.host + "]:" + std::to_string(address.port);

        return address.host + ":" + std::to_string(address.port);
    }

    inline std::string packetAddressToString(const PacketAddress& address, bool writePort, char portDelimiter)
    {
        if (!writePort)
            return address.host;

        if (portDelimiter == ':' && address.host.find(':') != std::string::npos)
            return "[" + address.host + "]:" + std::to_string(address.port);

        return address.host + portDelimiter + std::to_string(address.port);
    }

    template <class Stream>
    inline bool writePacketAddress(Stream& stream, const PacketAddress& address)
    {
        if (address.host.size() > std::numeric_limits<std::uint16_t>::max())
            return false;

        const std::uint16_t hostSize = static_cast<std::uint16_t>(address.host.size());
        stream.Write(hostSize);
        if (hostSize > 0)
            stream.Write(address.host.data(), hostSize);
        stream.Write(address.port);
        return true;
    }

    template <class Stream>
    inline bool readPacketAddress(Stream& stream, PacketAddress& address)
    {
        std::uint16_t hostSize = 0;
        if (!stream.Read(hostSize))
            return false;

        std::string host(hostSize, '\0');
        if (hostSize > 0 && !stream.Read(host.data(), hostSize))
            return false;

        unsigned short port = 0;
        if (!stream.Read(port))
            return false;

        address = makePacketAddress(host.c_str(), port);
        return true;
    }

    inline bool isPacketGuidAssigned(PacketGuid guid)
    {
        return guid != unassignedPacketGuid();
    }

    inline bool isPacketAddressAssigned(const PacketAddress& address)
    {
        return !address.host.empty();
    }
}

#endif
