#ifndef OPENMW_MP_ENDPOINT_HPP
#define OPENMW_MP_ENDPOINT_HPP

#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace mwmp
{
    constexpr unsigned short defaultTes3mpPort = 25565;

    struct ServerEndpoint
    {
        std::string host;
        unsigned short port = defaultTes3mpPort;
    };

    inline std::string trimEndpoint(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return {};

        const auto last = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(first, last - first + 1));
    }

    inline std::optional<unsigned short> parseEndpointPort(std::string_view value)
    {
        if (value.empty())
            return std::nullopt;

        unsigned int port = 0;
        for (unsigned char character : value)
        {
            if (!std::isdigit(character))
                return std::nullopt;

            port = port * 10 + static_cast<unsigned int>(character - '0');
            if (port > std::numeric_limits<unsigned short>::max())
                return std::nullopt;
        }

        if (port == 0)
            return std::nullopt;

        return static_cast<unsigned short>(port);
    }

    inline ServerEndpoint parseServerEndpoint(std::string_view endpoint, unsigned short defaultPort = defaultTes3mpPort)
    {
        const std::string text = trimEndpoint(endpoint);
        if (text.empty())
            return { "", defaultPort };

        if (text.front() == '[')
        {
            const std::string::size_type closingBracket = text.find(']');
            if (closingBracket != std::string::npos)
            {
                unsigned short port = defaultPort;
                if (closingBracket + 1 < text.size() && text[closingBracket + 1] == ':')
                {
                    if (const auto parsedPort = parseEndpointPort(std::string_view(text).substr(closingBracket + 2)))
                        port = *parsedPort;
                }

                return { text.substr(1, closingBracket - 1), port };
            }
        }

        const std::string::size_type firstColon = text.find(':');
        const std::string::size_type lastColon = text.rfind(':');
        if (firstColon != std::string::npos && firstColon == lastColon)
        {
            const std::string_view portText = std::string_view(text).substr(lastColon + 1);
            if (const auto parsedPort = parseEndpointPort(portText))
                return { text.substr(0, lastColon), *parsedPort };

            return { text.substr(0, lastColon), defaultPort };
        }

        return { text, defaultPort };
    }

    inline std::string formatServerEndpoint(std::string_view host, unsigned short port)
    {
        std::string hostText = trimEndpoint(host);
        if (hostText.size() >= 2 && hostText.front() == '[' && hostText.back() == ']')
            hostText = hostText.substr(1, hostText.size() - 2);

        if (hostText.find(':') != std::string::npos)
            return "[" + hostText + "]:" + std::to_string(port);

        return hostText + ":" + std::to_string(port);
    }
}

#endif
