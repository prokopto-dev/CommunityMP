#ifndef OPENMW_OBJECTPACKET_HPP
#define OPENMW_OBJECTPACKET_HPP

#include <string>
#include <components/openmw-mp/Base/BaseObject.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>


namespace mwmp
{
    class ObjectPacket : public BasePacket
    {
    public:
        ObjectPacket();

        ~ObjectPacket();

        void setObjectList(BaseObjectList *newObjectList);

        virtual void Packet(PacketStream *newBitstream, bool send);

        bool carriesCellData() const;

    protected:
        virtual void Object(BaseObject &baseObject, bool send);
        bool PacketHeader(PacketStream *newBitstream, bool send);
        BaseObjectList *objectList;
        static const int maxObjects = 3000;
        bool hasCellData;
    };
}

#endif //OPENMW_OBJECTPACKET_HPP
