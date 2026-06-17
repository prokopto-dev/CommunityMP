#ifndef OPENMW_LOCALPLAYER_HPP
#define OPENMW_LOCALPLAYER_HPP

#include <cstdint>
#include <unordered_map>

#include <components/esm/position.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include "../mwmechanics/activespells.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/timestamp.hpp"

namespace mwmp
{
    class Networking;
    class LocalPlayer : public BasePlayer
    {
    public:

        LocalPlayer();
        virtual ~LocalPlayer();

        time_t deathTime;
        bool receivedCharacter;
        bool receivedCell;

        bool isUsingBed;
        bool avoidSendingInventoryPackets;
        bool isReceivingQuickKeys;
        bool isPlayingAnimation;
        bool diedSinceArrestAttempt;
        unsigned int lastEnchantmentQuantity;

        void update();

        bool processCharGen();
        bool hasLoadedCharacter() const;
        bool isLoggedIn() const;
        bool canSendJournalChanges();
        bool isApplyingServerTopicChanges() const;
        void expectServerEquipmentReload();
        void completeServerEquipmentReload();
        void setCharGenBaseInfo(const ESM::NPC& npc);
        void setCharGenClass(const ESM::Class& charClass);

        void updateStatsDynamic(bool forceUpdate = false);
        void updateAttributes(bool forceUpdate = false);
        void updateSkills(bool forceUpdate = false);
        void updateLevel(bool forceUpdate = false);
        void updateBounty(bool forceUpdate = false);
        void updateReputation(bool forceUpdate = false);
        void updatePosition(bool forceUpdate = false, bool reliable = false, bool sendPacket = true,
            bool advanceSequenceWithoutPacket = false);
        void updateCell(bool forceUpdate = false, bool sendPositionPacket = true);
        void updateEquipment(bool forceUpdate = false);
        void updateInventory(bool forceUpdate = false);
        void updateAttackOrCast();
        void updateAnimFlags(bool forceUpdate = false);
        void updateDynamicObjects(float dt);

        void addItems();
        void addSpells();
        void addSpellsActive();
        void addJournalItems();
        void addTopics();

        void removeItems();
        void removeSpells();
        void removeSpellsActive();

        void die();
        void resurrect();

        void closeInventoryWindows();
        void updateInventoryWindow();

        void setCharacter();
        void setDynamicStats();
        void setAttributes();
        void setSkills();
        void setLevel();
        void setBounty();
        void setReputation();
        void setPosition();
        void setMomentum();
        void setCell();
        void setClass();
        void setEquipment();
        void setInventory();
        void restoreEquipmentFromInventory();
        void setSpellbook();
        void setSpellsActive();
        void setCooldowns();
        void setQuickKeys();
        void setFactions();
        void setBooks();
        void setShapeshift();
        void setMarkLocation();
        void setSelectedSpell();
        void setSelectedEnchantedItem();

        void sendDeath(char newDeathState);
        void sendClass();
        void sendInventory();
        void sendItemChange(const mwmp::Item& item, unsigned int action);
        void sendItemChange(const MWWorld::Ptr& itemPtr, int count, unsigned int action);
        void sendItemChange(const std::string& refId, int count, unsigned int action);
        void sendStoredItemRemovals();
        void sendSpellbook();
        void sendSpellChange(std::string id, unsigned int action);
        void sendSpellsActive();
        void sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params);
        void sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp);
        void sendCooldownChange(std::string id, int startTimestampDay, float startTimestampHour);
        void sendQuickKey(unsigned short slot, int type, const std::string& itemId = "");
        void sendJournalEntry(const std::string& quest, int index, const MWWorld::Ptr& actor);
        void sendJournalIndex(const std::string& quest, int index);
        void sendJournalFinished(const std::string& quest, bool isFinished);
        void sendFactionRank(const std::string& factionId, int rank);
        void sendFactionExpulsionState(const std::string& factionId, bool isExpelled);
        void sendFactionReputation(const std::string& factionId, int reputation);
        void sendTopic(const std::string& topic);
        void sendBook(const std::string& bookId);
        void sendWerewolfState(bool isWerewolf);
        void sendMarkLocation(const ESM::Cell& newMarkCell, const ESM::Position& newMarkPosition);
        void sendSelectedSpell(const std::string& newSelectedSpellId);
        void sendSelectedEnchantedItem(const mwmp::Item& newSelectedEnchantedItem);
        void sendItemUse(const MWWorld::Ptr& itemPtr, bool usingItemMagic = false, char currentDrawState = 0);
        void sendCellStates();
        void queueCellChangeReason(unsigned int reason);

        void clearCellStates();
        void clearCurrentContainer();

        void storeCellState(const ESM::Cell& cell, int stateType);
        void storeCurrentContainer(const MWWorld::Ptr& container);
        void storeItemRemoval(const std::string& refId, int count);
        void storeLastEnchantmentQuantity(unsigned int quantity);

        void playAnimation();
        void playSpeech();

        MWWorld::Ptr getPlayerPtr();

    private:
        Networking *getNetworking();
        int mPendingCharGenStage = -1;
        ESM::NPC mCharGenBaseInfo;
        ESM::Class mCharGenClass;
        bool mHasCharGenClass = false;
        bool mApplyingServerTopicChanges = false;
        bool mApplyingServerJournalLoad = false;
        bool mApplyingServerTopicLoad = false;
        bool mApplyingServerBookLoad = false;
        float mServerEquipmentReloadTimer = 0.f;
        bool mHasPendingCellChangePositionSequence = false;
        std::uint32_t mPendingCellChangePositionSequence = 0;
        float mDynamicObjectSyncTimer = 0.f;

        struct DynamicObjectSyncState
        {
            ESM::Position mLastSent;
            bool mHasLastSent = false;
            bool mLastActive = false;
        };
        std::unordered_map<const MWWorld::LiveCellRefBase*, DynamicObjectSyncState> mDynamicObjectSyncStates;

    };
}

#endif //OPENMW_LOCALPLAYER_HPP

