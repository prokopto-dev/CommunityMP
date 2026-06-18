#ifndef OPENMW_SERVERCELL_HPP
#define OPENMW_SERVERCELL_HPP

#include <deque>
#include <cstddef>
#include <string>
#include <vector>
#include <components/esm/records.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>
#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

class Player;
class Cell;

class Cell
{
    friend class CellController;
public:
    struct ServerWorldReference
    {
        std::string refKey;
        std::string refId;
        std::string baseRecordType;
        std::string baseRecordCategory;
        std::string baseRecordSourceFile;
        bool baseActorAiAvailable = false;
        std::size_t baseActorAiPackageCount = 0;
        unsigned int baseActorAiAction = 0;
        unsigned int baseActorAiDistance = 0;
        unsigned int baseActorAiDuration = 0;
        bool baseActorAiShouldRepeat = false;
        ESM::Position baseActorAiCoordinates;
        std::string baseActorAiTargetId;
        std::string baseActorAiCellName;
        unsigned int baseActorAiHello = 0;
        unsigned int baseActorAiFight = 0;
        unsigned int baseActorAiFlee = 0;
        unsigned int baseActorAiAlarm = 0;
        unsigned int refNum = 0;
        unsigned int mpNum = 0;
        int refNumContentFile = -1;
        int count = 1;
        float scale = 1.f;
        ESM::Position position;
        bool moved = false;
        bool teleport = false;
        bool locked = false;
        int lockLevel = 0;
        std::string destinationCell;
        ESM::Position destinationPosition;
        bool baseRecordResolved = false;
        bool baseRecordAmbiguous = false;
        bool baseRecordDeleted = false;
    };

    struct ServerWorldBootstrapStats
    {
        bool attempted = false;
        bool loaded = false;
        std::string cellKey;
        std::size_t referenceCount = 0;
        std::size_t actorCount = 0;
        std::size_t containerCount = 0;
        std::size_t doorCount = 0;
        std::size_t itemCount = 0;
        std::size_t staticCount = 0;
        std::size_t activatorCount = 0;
        std::size_t actorAiCount = 0;
        std::size_t unresolvedCount = 0;
        std::size_t ambiguousCount = 0;
        std::size_t deletedBaseRecordCount = 0;
    };

    Cell(ESM::Cell cell);
    typedef std::deque<Player*> TPlayers;
    typedef TPlayers::const_iterator Iterator;

    Iterator begin() const;
    Iterator end() const;

    void addPlayer(Player *player);
    void removePlayer(Player *player, bool cleanPlayer = true);

    void readActorList(unsigned char packetID, const mwmp::BaseActorList *newActorList);
    bool containsActor(int refNum, int mpNum);
    mwmp::BaseActor *getActor(int refNum, int mpNum);
    void upsertActors(const mwmp::BaseActorList *newActorList);
    void removeActors(const mwmp::BaseActorList *newActorList);
    void requestActorListFrom(const mwmp::PacketGuid& guid);
    bool hasPendingActorListRequest() const;
    bool hasPendingActorListRequestFrom(const mwmp::PacketGuid& guid) const;
    bool consumePendingActorListRequestFrom(const mwmp::PacketGuid& guid);
    bool hasActorListSnapshot() const;
    void ensureServerWorldStateBootstrapped();
    bool hasServerWorldStateBootstrap() const;
    const ServerWorldBootstrapStats& getServerWorldBootstrapStats() const;
    const std::vector<ServerWorldReference>& getServerWorldReferences() const;
    const mwmp::BaseActorList& getServerWorldActorList() const;
    bool seedActorListFromServerWorldState();
    bool hasServerWorldSeededActorList() const;
    std::size_t getServerWorldSeededActorCount() const;

    mwmp::PacketGuid *getAuthority();
    void setAuthority(const mwmp::PacketGuid& guid);
    bool hasAuthority(const mwmp::PacketGuid& guid) const;
    mwmp::BaseActorList *getActorList();
    const ESM::Cell& getCellData() const;

    TPlayers getPlayers() const;
    bool hasPlayers() const;
    bool hasPlayer(const Player* player) const;
    bool hasSimulationInterest() const;
    void setSimulationInterest(bool enabled);
    void sendToLoaded(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList) const;
    void sendToLoadedAndGuids(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList,
        const std::vector<mwmp::PacketGuid>& targetGuids) const;
    void sendToLoaded(mwmp::ObjectPacket *objectPacket, mwmp::BaseObjectList *baseObjectList) const;

    std::string getShortDescription() const;


private:
    TPlayers players;
    ESM::Cell cell;

    mwmp::PacketGuid authorityGuid;
    mwmp::PacketGuid actorListRequestGuid;
    mwmp::BaseActorList cellActorList;
    mwmp::BaseActorList serverWorldActorList;
    std::vector<ServerWorldReference> serverWorldReferences;
    ServerWorldBootstrapStats serverWorldBootstrapStats;
    bool actorListSnapshotReceived;
    bool serverWorldActorListSeeded;
    std::size_t serverWorldSeededActorCount;
    bool simulationInterest;
};


#endif //OPENMW_SERVERCELL_HPP
