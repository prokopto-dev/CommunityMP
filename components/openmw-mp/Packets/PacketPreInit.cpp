#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketPreInit.hpp"

mwmp::PacketPreInit::PacketPreInit() : BasePacket()
{
    packetID = ID_GAME_PREINIT;
    checksums = nullptr;
    protocolVersion = 0;
}

void mwmp::PacketPreInit::Packet(PacketStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);

    if (checksums == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "PacketPreInit has no checksum container");
        packetValid = false;
        return;
    }

    if (!RW(version, send, false, versionMaxLength) ||
        !RW(protocolVersion, send) ||
        !RW(commitHash, send, false, commitHashMaxLength))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Could not read pre-init protocol version information");
        packetValid = false;
        return;
    }

    uint32_t numberOfChecksums = send ? static_cast<uint32_t>(checksums->size()) : 0;
    if (!RW(numberOfChecksums, send))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Could not read pre-init checksum count");
        packetValid = false;
        return;
    }

    if (numberOfChecksums > maxPlugins)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong number of checksums %d when maximum is %d", numberOfChecksums, maxPlugins);
        packetValid = false;
        return;
    }

    struct NAS
    {
        uint32_t hashN;
        uint32_t strSize;
    };

    std::vector<NAS> NumberOfHashesAndStrSizes(numberOfChecksums);

    PluginContainer::const_iterator checksumIt = checksums->begin();

    for (auto &&nas : NumberOfHashesAndStrSizes)
    {
        if (send)
        {
            nas.strSize = static_cast<uint32_t>(checksumIt->first.size());
            nas.hashN = static_cast<uint32_t>(checksumIt++->second.size());
        }
        if (!RW(nas, send))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Could not read pre-init checksum metadata");
            packetValid = false;
            return;
        }

        if (nas.strSize > pluginNameMaxLength)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong string length %d when maximum length is %d",
                        nas.strSize,
                        pluginNameMaxLength);
        else if (nas.hashN > maxHashes)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong  number of hashes %d when maximum is %d", nas.hashN, maxHashes);
        else
            continue;
        packetValid = false;
        return;
    }

    if (numberOfChecksums == 0) // server accepted plugin list via sending "empty" packet
    {
        if (!send)
            checksums->clear();
        return;
    }

    checksums->resize(numberOfChecksums);

    auto numberOfHashesIt = NumberOfHashesAndStrSizes.cbegin();

    for (auto &&checksum : *checksums)
    {
        if (!RW(checksum.first, send, false, numberOfHashesIt->strSize))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Could not read pre-init plugin name");
            packetValid = false;
            return;
        }

        checksum.second.resize(numberOfHashesIt->hashN);
        for (auto &&hash : checksum.second)
        {
            if (!RW(hash, send))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Could not read pre-init plugin checksum");
                packetValid = false;
                return;
            }
        }
        ++numberOfHashesIt;
    }
}

void mwmp::PacketPreInit::setChecksums(mwmp::PacketPreInit::PluginContainer *newChecksums)
{
    checksums = newChecksums;
}

void mwmp::PacketPreInit::setProtocolVersionInfo(const std::string& newVersion, uint32_t newProtocolVersion,
    const std::string& newCommitHash)
{
    version = newVersion;
    protocolVersion = newProtocolVersion;
    commitHash = newCommitHash;
}

const std::string& mwmp::PacketPreInit::getVersion() const
{
    return version;
}

uint32_t mwmp::PacketPreInit::getProtocolVersion() const
{
    return protocolVersion;
}

const std::string& mwmp::PacketPreInit::getCommitHash() const
{
    return commitHash;
}
