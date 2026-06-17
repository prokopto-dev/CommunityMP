#ifndef OPENMW_DEDICATEDPLAYER_HPP
#define OPENMW_DEDICATEDPLAYER_HPP

#include <components/esm3/custommarkerstate.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include "../mwclass/npc.hpp"

#include "../mwmechanics/aisequence.hpp"

#include "../mwworld/manualref.hpp"

#include <map>

namespace MWMechanics
{
    class Actor;
}

namespace mwmp
{
    class DedicatedPlayer : public BasePlayer
    {
        friend class PlayerList;

    public:

        void update(float dt);

        void move(float dt);
        bool readPositionPacket();
        bool normalizePositionPacket();
        void setBaseInfo();
        void setStatsDynamic();
        void restoreDynamicStats();
        void setAnimFlags();
        void setAttributes();
        void setSkills();
        void setEquipment();
        void setShapeshift();
        void setCell();

        void playAnimation();
        void playSpeech();

        void equipItem(std::string itemId, bool noSound = false);
        void die();
        void resurrect();

        void addSpellsActive();
        void removeSpellsActive();
        void setSpellsActive();
        void applySpellsActiveChanges();

        void updateMarker();
        void removeMarker();
        void enableMarker();

        void createReference(const ESM::RefId& recId);
        void deleteReference();

        MWWorld::Ptr getPtr();
        MWWorld::ManualRef* getRef();
        bool hasReference() const;

        void setPtr(const MWWorld::Ptr& newPtr);
        void reloadPtr();

    private:

        DedicatedPlayer(PacketGuid guid);
        virtual ~DedicatedPlayer();

        MWWorld::ManualRef* reference;

        MWWorld::Ptr ptr;

        ESM::CustomMarker marker;
        bool markerEnabled;

        ESM::RefId previousRace;
        std::string previousCreatureRefId;
        bool previousDisplayCreatureName;

        ESM::RefId creatureRecordId;

        bool hasReceivedInitialEquipment;
        bool hasFinishedInitialTeleportation;
        bool hasReceivedInitialPosition;
        bool hasChangedCell;
        bool isLevitationPurged;
        bool hasPendingSpellsActiveChanges;

        bool wasJumping;

        void setPosition();
        void setMovementSettings();
        void setMovementSettings(const ESM::Position& movementDirection);
        void setMovementSettingsFromVisualDelta(const ESM::Position& previousPosition);
        void applyRemoteJumpMovementCue(bool wasRemoteJumping);
    };
}
#endif //OPENMW_DEDICATEDPLAYER_HPP

