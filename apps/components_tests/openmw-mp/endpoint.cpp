#include <components/openmw-mp/Endpoint.hpp>

#include <gtest/gtest.h>

namespace
{
    TEST(Tes3mpEndpointTest, hostWithoutPortUsesDefaultPort)
    {
        const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint(" example.org ");

        EXPECT_EQ(endpoint.host, "example.org");
        EXPECT_EQ(endpoint.port, mwmp::defaultTes3mpPort);
    }

    TEST(Tes3mpEndpointTest, hostWithPortUsesExplicitPort)
    {
        const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint("example.org:26000");

        EXPECT_EQ(endpoint.host, "example.org");
        EXPECT_EQ(endpoint.port, 26000);
    }

    TEST(Tes3mpEndpointTest, invalidOrZeroPortFallsBackToDefaultPort)
    {
        const mwmp::ServerEndpoint invalidPort = mwmp::parseServerEndpoint("example.org:not-a-port");
        const mwmp::ServerEndpoint zeroPort = mwmp::parseServerEndpoint("127.0.0.1:0");

        EXPECT_EQ(invalidPort.host, "example.org");
        EXPECT_EQ(invalidPort.port, mwmp::defaultTes3mpPort);
        EXPECT_EQ(zeroPort.host, "127.0.0.1");
        EXPECT_EQ(zeroPort.port, mwmp::defaultTes3mpPort);
    }

    TEST(Tes3mpEndpointTest, bareIpv6AddressUsesDefaultPort)
    {
        const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint("::1");

        EXPECT_EQ(endpoint.host, "::1");
        EXPECT_EQ(endpoint.port, mwmp::defaultTes3mpPort);
    }

    TEST(Tes3mpEndpointTest, bracketedIpv6AddressUsesExplicitPort)
    {
        const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint("[2001:db8::15]:26001");

        EXPECT_EQ(endpoint.host, "2001:db8::15");
        EXPECT_EQ(endpoint.port, 26001);
    }

    TEST(Tes3mpEndpointTest, bracketedIpv6WithInvalidPortFallsBackToDefaultPort)
    {
        const mwmp::ServerEndpoint endpoint = mwmp::parseServerEndpoint("[2001:db8::15]:99999");

        EXPECT_EQ(endpoint.host, "2001:db8::15");
        EXPECT_EQ(endpoint.port, mwmp::defaultTes3mpPort);
    }

    TEST(Tes3mpEndpointTest, formatBracketsIpv6Address)
    {
        EXPECT_EQ(mwmp::formatServerEndpoint("2001:db8::15", 26001), "[2001:db8::15]:26001");
    }
}
