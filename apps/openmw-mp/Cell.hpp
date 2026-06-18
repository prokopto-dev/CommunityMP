#ifndef OPENMW_SERVERCELL_HPP
#define OPENMW_SERVERCELL_HPP

#include <deque>
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
    bool actorListSnapshotReceived;
    bool simulationInterest;
};


#endif //OPENMW_SERVERCELL_HPP
