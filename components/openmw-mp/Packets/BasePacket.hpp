#ifndef OPENMW_BASEPACKET_HPP
#define OPENMW_BASEPACKET_HPP

#include <components/esm/refid.hpp>
#include <components/esm3/refnum.hpp>
#include <components/openmw-mp/Transport/PacketDelivery.hpp>
#include <components/openmw-mp/Transport/PacketDestination.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/openmw-mp/Transport/PacketId.hpp>
#include <components/openmw-mp/Transport/PacketStream.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace mwmp
{
    class PacketTransport;

    class BasePacket
    {
    public:
        BasePacket();

        virtual ~BasePacket() = default;

        virtual void Packet(PacketStream *newBitstream, bool send);
        virtual uint32_t Send(bool toOtherPlayers = true);
        virtual uint32_t Send(const PacketDestination& destination);
        uint32_t SendWithReliability(bool toOtherPlayers, PacketReliability forcedReliability);
        uint32_t SendWithReliability(const PacketDestination& destination, PacketReliability forcedReliability);
        virtual void Read();

        void setGUID(PacketGuid newGuid);
        PacketGuid getGUID();

        void SetReadStream(PacketStream *bitStream);
        void SetSendStream(PacketStream *bitStream);
        void SetStreams(PacketStream *inStream, PacketStream *outStream);
        virtual uint32_t RequestData(PacketGuid targetGuid);

        static void SetPacketTransport(PacketTransport *transport);
        static PacketTransport *GetPacketTransport();

        static inline uint32_t headerSize()
        {
            return static_cast<uint32_t>(1 + packetGuidSize()); // packetID + packet GUID (uint64_t)
        }

        PacketId GetPacketID() const
        {
            return packetID;
        }

        bool isPacketValid() const
        {
            return packetValid;
        }

    protected:
        template<class templateType>
        bool RW(templateType &data, uint32_t size, bool write)
        {
            if (write)
                bs->Write(data, size);
            else if (!bs->Read(data, size))
            {
                packetValid = false;
                return false;
            }
            return true;
        }

        template<class templateType>
        bool RW(templateType &data, bool write, bool compress = 0)
        {
            if (write)
            {
                if (compress)
                    bs->WriteCompressed(data);
                else
                    bs->Write(data);
                return true;
            }
            else
            {
                if (compress)
                {
                    if (bs->ReadCompressed(data))
                        return true;
                }
                else if (bs->Read(data))
                    return true;

                packetValid = false;
                return false;
            }
        }

        bool RW(bool &data, bool write)
        {
            if (write)
                bs->Write(data);
            else if (!bs->Read(data))
            {
                packetValid = false;
                return false;
            }
            return true;
        }

        const static uint32_t maxStrSize = 64 * 1024; // 64 KiB

        bool RW(std::string &str, bool write, bool compress = false, std::string::size_type maxSize = maxStrSize)
        {
            (void)compress;
            if (write)
            {
                const std::string::size_type writeSize = std::min({ str.size(), maxSize,
                    static_cast<std::string::size_type>(std::numeric_limits<uint32_t>::max()) });
                const uint32_t serializedSize = static_cast<uint32_t>(writeSize);
                bs->Write(serializedSize);
                if (serializedSize > 0)
                    bs->Write(str.data(), serializedSize);
                return true;
            }

            uint32_t serializedSize = 0;
            if (!bs->Read(serializedSize))
            {
                packetValid = false;
                return false;
            }

            if (serializedSize > maxStrSize)
            {
                packetValid = false;
                str = std::string();
                return false;
            }

            std::string value(serializedSize, '\0');
            if (serializedSize > 0 && !bs->Read(value.data(), serializedSize))
            {
                packetValid = false;
                str = std::string();
                return false;
            }

            value.resize(std::min<std::string::size_type>(value.size(), maxSize));
            str = std::move(value);
            return true;
        }

        bool RW(ESM::RefId &id, bool write, bool compress = false, std::string::size_type maxSize = maxStrSize)
        {
            std::string value;
            if (write)
                value = id.serializeText();

            bool res = RW(value, write, compress, maxSize);

            if (!res)
            {
                if (!write)
                    id = ESM::RefId();
                return false;
            }

            if (!write)
            {
                id = ESM::RefId::deserializeText(value);

                if (id.empty() && !value.empty())
                    id = ESM::RefId::stringRefId(value);
            }

            return res;
        }

        bool RW(std::variant<ESM::RefId, ESM::RefNum> &arg, bool write)
        {
            enum class ArgType : uint8_t
            {
                RefId = 0,
                RefNum = 1,
            };

            ArgType type = ArgType::RefId;

            if (write)
            {
                if (std::holds_alternative<ESM::RefNum>(arg))
                    type = ArgType::RefNum;
            }

            if (!RW(type, write))
                return false;

            if (!write && type != ArgType::RefId && type != ArgType::RefNum)
            {
                packetValid = false;
                return false;
            }

            if (type == ArgType::RefNum)
            {
                ESM::RefNum refNum;
                if (write)
                    refNum = std::get<ESM::RefNum>(arg);

                bool res = RW(refNum.mIndex, write) && RW(refNum.mContentFile, write);

                if (!write)
                    arg = refNum;

                return res;
            }

            ESM::RefId refId;
            if (write)
                refId = std::get<ESM::RefId>(arg);

            bool res = RW(refId, write, true);

            if (!write)
                arg = refId;

            return res;
        }

    protected:
        uint8_t packetID;
        PacketReliability reliability;
        PacketPriority priority;
        int8_t orderChannel;
        PacketStream *bsRead, *bsSend, *bs;
        PacketGuid guid;
        bool packetValid;

    private:
        static PacketTransport *sTransport;
    };
}

#endif //OPENMW_BASEPACKET_HPP
