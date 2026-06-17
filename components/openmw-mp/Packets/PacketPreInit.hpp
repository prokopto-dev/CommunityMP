#ifndef OPENMW_PACKETPREINIT_HPP
#define OPENMW_PACKETPREINIT_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "BasePacket.hpp"


namespace mwmp
{
    class PacketPreInit : public BasePacket
    {
    public:
        typedef std::vector<uint32_t> HashList;
        typedef std::pair<std::string, HashList> PluginPair;
        typedef std::vector<PluginPair> PluginContainer;

        PacketPreInit();

        virtual void Packet(PacketStream *newBitstream, bool send);
        void setChecksums(PluginContainer *checksums);
        void setProtocolVersionInfo(const std::string& version, uint32_t protocolVersion, const std::string& commitHash);
        const std::string& getVersion() const;
        uint32_t getProtocolVersion() const;
        const std::string& getCommitHash() const;
    private:
        PluginContainer *checksums;
        const static uint32_t maxPlugins = 1000;
        const static uint32_t pluginNameMaxLength = 256;
        const static uint32_t maxHashes = 50;
        const static uint32_t versionMaxLength = 32;
        const static uint32_t commitHashMaxLength = 64;

        std::string version;
        uint32_t protocolVersion;
        std::string commitHash;
    };
}


#endif //OPENMW_PACKETPREINIT_HPP
