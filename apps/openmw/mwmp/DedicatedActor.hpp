#ifndef OPENMW_DEDICATEDACTOR_HPP
#define OPENMW_DEDICATEDACTOR_HPP

#include <components/openmw-mp/Base/BaseActor.hpp>
#include "../mwmechanics/aisequence.hpp"
#include "../mwworld/manualref.hpp"

#include <chrono>
#include <osg/Vec3f>

namespace mwmp
{
    class DedicatedActor : public BaseActor
    {
    public:

        DedicatedActor();
        virtual ~DedicatedActor();

        void update(float dt);
        void move(float dt);
        void setCell(MWWorld::CellStore *cellStore);
        void setMovementSettings();
        void setPosition();
        void setAnimFlags();
        void setStatsDynamic();
        void restoreDynamicStats();
        void setEquipment();
        void setAi();
        void playAnimation();
        void playSound();
        void updateRemoteMovementEstimate(const ESM::Position& previousPosition, bool hadPositionData);
        void resetRemoteMovementEstimate();

        bool hasItem(std::string itemId, int charge);
        void equipItem(std::string itemId, int charge, bool noSound = false);

        void addSpellsActive();
        void removeSpellsActive();
        void setSpellsActive();

        MWWorld::Ptr getPtr();
        void setPtr(const MWWorld::Ptr& newPtr);
        void reloadPtr();

    private:
        MWWorld::Ptr ptr;

        bool hasReceivedInitialEquipment;
        bool hasChangedCell;
        bool wasJumping;
        osg::Vec3f mRemoteVelocity;
        std::chrono::steady_clock::time_point mLastRemotePositionPacket;
        float mSmoothedRemoteSampleIntervalSeconds;
        float mSmoothedRemoteLatencySeconds;
        float mRemoteJitterSeconds;
        float mRemotePacketAgeSeconds;
        bool mHasRemoteVelocity;
        bool mHasRemoteTimingEstimate;

        void setMovementSettings(const ESM::Position& movementDirection);
        void setMovementSettingsFromVisualDelta(const ESM::Position& previousPosition);
        void applyRemoteJumpMovementCue(bool wasRemoteJumping);
        void updateRemoteTimingEstimate(float arrivalDeltaSeconds, bool hasPreviousPacket);
        void resetRemoteTimingEstimate();
    };
}

#endif //OPENMW_DEDICATEDACTOR_HPP

