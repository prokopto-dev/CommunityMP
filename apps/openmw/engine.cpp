#include "engine.hpp"
#include "serversimulationmode.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

#include <osgDB/ReaderWriter>
#include <osgDB/Registry>
#include <osgViewer/ViewerEventHandlers>

#include <SDL.h>

#include <components/debug/debuglog.hpp>
#include <components/debug/gldebug.hpp>

#include <components/misc/rng.hpp>
#include <components/misc/strings/format.hpp>

#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <components/sdlutil/imagetosurface.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/resource/stats.hpp>

#include <components/compiler/extensions0.hpp>

#include <components/stereo/stereomanager.hpp>

#include <components/sceneutil/glextensions.hpp>
#include <components/sceneutil/workqueue.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>

#include <components/version/version.hpp>

#include <components/l10n/manager.hpp>

#include <components/loadinglistener/asynclistener.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include <components/misc/frameratelimiter.hpp>

#include <components/sceneutil/color.hpp>
#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/util.hpp>

#include <components/settings/shadermanager.hpp>
#include <components/settings/values.hpp>

#ifdef BUILD_TES3MP_CLIENT
#include <components/openmw-mp/Branding.hpp>
#include <components/openmw-mp/ClientSettings.hpp>
#endif
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadrace.hpp>

#include "../openmw-mp/SimulationRuntime.hpp"

#include "mwinput/inputmanagerimp.hpp"

#include "mwgui/windowmanagerimp.hpp"

#include "mwlua/luamanagerimp.hpp"
#include "mwlua/worker.hpp"

#include "mwscript/interpretercontext.hpp"
#include "mwscript/scriptmanagerimp.hpp"

#include "mwmp/CellIdentity.hpp"
#include "mwmp/Main.hpp"
#include "mwmp/ScriptController.hpp"

#include "mwsound/constants.hpp"
#include "mwsound/soundmanagerimp.hpp"

#include "mwworld/class.hpp"
#include "mwworld/action.hpp"
#include "mwworld/cellstore.hpp"
#include "mwworld/containerstore.hpp"
#include "mwworld/esmstore.hpp"
#include "mwworld/inventorystore.hpp"
#include "mwworld/datetimemanager.hpp"
#include "mwworld/scene.hpp"
#include "mwworld/worldimp.hpp"

#include "mwrender/vismask.hpp"

#include "mwclass/classes.hpp"

#include "mwdialogue/dialoguemanagerimp.hpp"
#include "mwdialogue/journalimp.hpp"
#include "mwdialogue/scripttest.hpp"

#include "mwmechanics/aisequence.hpp"
#include "mwmechanics/aipackage.hpp"
#include "mwmechanics/creaturestats.hpp"
#include "mwmechanics/mechanicsmanagerimp.hpp"
#include "mwmechanics/movement.hpp"
#include "mwmechanics/npcstats.hpp"

#include "mwstate/statemanagerimp.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "mwmp/GUIController.hpp"
#include "mwmp/LocalPlayer.hpp"
#include "mwmp/Main.hpp"
#endif

#include "profile.hpp"

namespace
{
    void checkSDLError(int ret)
    {
        if (ret != 0)
            Log(Debug::Error) << "SDL error: " << SDL_GetError();
    }

    void initStatsHandler(Resource::Profiler& profiler)
    {
        const osg::Vec4f textColor(1.f, 1.f, 1.f, 1.f);
        const osg::Vec4f barColor(1.f, 1.f, 1.f, 1.f);
        const float multiplier = 1000;
        const bool average = true;
        const bool averageInInverseSpace = false;
        const float maxValue = 10000;

        OMW::forEachUserStatsValue([&](const OMW::UserStats& v) {
            profiler.addUserStatsLine(v.mLabel, textColor, barColor, v.mTaken, multiplier, average,
                averageInInverseSpace, v.mBegin, v.mEnd, maxValue);
        });
        // the forEachUserStatsValue loop is "run" at compile time, hence the settings manager is not available.
        // Unconditionnally add the async physics stats, and then remove it at runtime if necessary
        if (Settings::physics().mAsyncNumThreads == 0)
            profiler.removeUserStatsLine(" -Async");
    }

    struct ScreenCaptureMessageBox
    {
        void operator()(std::string filePath) const
        {
            if (filePath.empty())
            {
                MWBase::Environment::get().getWindowManager()->scheduleMessageBox(
                    "#{OMWEngine:ScreenshotFailed}", MWGui::ShowInDialogueMode_Never);

                return;
            }

            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            std::string message = l10n->formatMessage("ScreenshotMade", { "file" }, { L10n::toUnicode(filePath) });

            MWBase::Environment::get().getWindowManager()->scheduleMessageBox(
                std::move(message), MWGui::ShowInDialogueMode_Never);
        }
    };

    struct IgnoreString
    {
        void operator()(std::string) const {}
    };

    std::string readWholeFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
            return {};

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    void appendJsonString(std::string& result, std::string_view value)
    {
        constexpr char hex[] = "0123456789abcdef";
        result.push_back('"');
        for (unsigned char c : value)
        {
            switch (c)
            {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        result += "\\u00";
                        result.push_back(hex[(c >> 4) & 0x0f]);
                        result.push_back(hex[c & 0x0f]);
                    }
                    else
                        result.push_back(static_cast<char>(c));
            }
        }
        result.push_back('"');
    }

    void appendJsonStringField(std::string& result, std::string_view name, std::string_view value)
    {
        appendJsonString(result, name);
        result.push_back(':');
        appendJsonString(result, value);
    }

    std::string readFlatJsonStringField(std::string_view json, std::string_view field)
    {
        std::string quotedField;
        appendJsonString(quotedField, field);
        std::size_t pos = json.find(quotedField);
        if (pos == std::string_view::npos)
            return {};

        pos = json.find(':', pos + quotedField.size());
        if (pos == std::string_view::npos)
            return {};

        pos = json.find('"', pos + 1);
        if (pos == std::string_view::npos)
            return {};

        std::string result;
        for (++pos; pos < json.size(); ++pos)
        {
            const char c = json[pos];
            if (c == '"')
                return result;

            if (c != '\\')
            {
                result.push_back(c);
                continue;
            }

            if (++pos >= json.size())
                break;

            switch (json[pos])
            {
                case '"':
                case '\\':
                case '/':
                    result.push_back(json[pos]);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                default:
                    result.push_back(json[pos]);
                    break;
            }
        }

        return {};
    }

    std::filesystem::path serverWorldManifestPath(const std::filesystem::path& savesPath)
    {
        if (savesPath.empty())
            return {};

        return savesPath.parent_path() / "manifest.json";
    }

    struct ServerWorldManifest
    {
        bool exists = false;
        bool matches = true;
        std::filesystem::path savePath;
    };

    ServerWorldManifest readServerWorldManifest(const std::filesystem::path& manifestPath,
        std::string_view contentPlanFingerprint, std::string_view worldDatabaseFingerprint,
        std::string_view serverWorldCompatibilityFingerprint)
    {
        ServerWorldManifest result;
        if (manifestPath.empty() || !std::filesystem::is_regular_file(manifestPath))
            return result;

        const std::string manifest = readWholeFile(manifestPath);
        if (manifest.empty())
            return result;

        result.exists = true;
        const std::string savedContentPlanFingerprint = readFlatJsonStringField(manifest, "contentPlanFingerprint");
        const std::string savedWorldDatabaseFingerprint
            = readFlatJsonStringField(manifest, "worldDatabaseFingerprint");
        const std::string savedServerWorldCompatibilityFingerprint
            = readFlatJsonStringField(manifest, "serverWorldCompatibilityFingerprint");
        const std::string savePath = readFlatJsonStringField(manifest, "savePath");

        if (!serverWorldCompatibilityFingerprint.empty() && !savedServerWorldCompatibilityFingerprint.empty())
            result.matches = savedServerWorldCompatibilityFingerprint == serverWorldCompatibilityFingerprint;
        else
            result.matches = (contentPlanFingerprint.empty() || savedContentPlanFingerprint == contentPlanFingerprint)
                && (worldDatabaseFingerprint.empty() || savedWorldDatabaseFingerprint == worldDatabaseFingerprint);
        if (!savePath.empty())
            result.savePath = Files::pathFromUnicodeString(savePath);

        return result;
    }

    void writeServerWorldManifestIfChanged(const std::filesystem::path& manifestPath,
        std::string_view contentPlanFingerprint, std::string_view worldDatabaseFingerprint,
        std::string_view serverWorldCompatibilityFingerprint, const std::filesystem::path& savePath)
    {
        if (manifestPath.empty())
            return;

        std::string result;
        result.reserve(512);
        result += "{\n  ";
        appendJsonStringField(result, "schema", "communitymp.openmw-server-world.v1");
        result += ",\n  ";
        appendJsonStringField(result, "contentPlanFingerprint", contentPlanFingerprint);
        result += ",\n  ";
        appendJsonStringField(result, "worldDatabaseFingerprint", worldDatabaseFingerprint);
        result += ",\n  ";
        appendJsonStringField(result, "serverWorldCompatibilityFingerprint", serverWorldCompatibilityFingerprint);
        result += ",\n  ";
        appendJsonStringField(result, "savePath", Files::pathToUnicodeString(savePath));
        result += "\n}\n";

        if (readWholeFile(manifestPath) == result)
            return;

        std::filesystem::create_directories(manifestPath.parent_path());
        std::ofstream stream(manifestPath, std::ios::binary);
        stream << result;
        if (stream.fail())
            throw std::runtime_error("failed to write server OpenMW world manifest");
    }

    const MWState::Slot* findMostRecentSaveSlot(
        MWState::StateManager& stateManager, const MWState::Character*& character)
    {
        character = nullptr;
        const MWState::Slot* slot = nullptr;

        for (auto characterIt = stateManager.characterBegin(); characterIt != stateManager.characterEnd(); ++characterIt)
        {
            const MWState::Character& candidateCharacter = *characterIt;
            for (auto slotIt = candidateCharacter.begin(); slotIt != candidateCharacter.end(); ++slotIt)
            {
                const MWState::Slot& candidateSlot = *slotIt;
                if (slot == nullptr || slot->mTimeStamp < candidateSlot.mTimeStamp)
                {
                    character = &candidateCharacter;
                    slot = &candidateSlot;
                }
            }
        }

        return slot;
    }

    class IdentifyOpenGLOperation : public osg::GraphicsOperation
    {
    public:
        IdentifyOpenGLOperation()
            : GraphicsOperation("IdentifyOpenGLOperation", false)
        {
        }

        void operator()(osg::GraphicsContext* graphicsContext) override
        {
            Log(Debug::Info) << "OpenGL Vendor: " << glGetString(GL_VENDOR);
            Log(Debug::Info) << "OpenGL Renderer: " << glGetString(GL_RENDERER);
            Log(Debug::Info) << "OpenGL Version: " << glGetString(GL_VERSION);
            glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &mMaxTextureImageUnits);
        }

        int getMaxTextureImageUnits() const
        {
            if (mMaxTextureImageUnits == 0)
                throw std::logic_error("mMaxTextureImageUnits is not initialized");
            return mMaxTextureImageUnits;
        }

    private:
        int mMaxTextureImageUnits = 0;
    };

    void reportStats(unsigned frameNumber, osgViewer::Viewer& viewer, std::ostream& stream)
    {
        viewer.getViewerStats()->report(stream, frameNumber);
        osgViewer::Viewer::Cameras cameras;
        viewer.getCameras(cameras);
        for (osg::Camera* camera : cameras)
            camera->getStats()->report(stream, frameNumber);
    }

    void copyDynamicStat(const MWMechanics::DynamicStat<float>& source, ESM::StatState<float>& destination)
    {
        destination.mBase = source.getBase();
        destination.mCurrent = source.getCurrent();
        destination.mMod = source.getModifier();
    }

    bool isFiniteDynamicStatState(const ESM::StatState<float>& stat)
    {
        return std::isfinite(stat.mBase) && std::isfinite(stat.mCurrent) && std::isfinite(stat.mMod);
    }

    bool hasFiniteSimpleCreatureStats(const mwmp::SimpleCreatureStats& stats)
    {
        return isFiniteDynamicStatState(stats.mDynamic[0])
            && isFiniteDynamicStatState(stats.mDynamic[1])
            && isFiniteDynamicStatState(stats.mDynamic[2]);
    }

    ESM::RefId makeServerSimulationPlayerActorRecordId(mwmp::PacketGuid guid)
    {
        return ESM::RefId::stringRefId("$communitymp_server_player_" + mwmp::packetGuidToString(guid));
    }

    bool hasDifferentServerSimulationPlayerBase(const OMW::ServerSimulationPlayerActorState& state,
        const mwmp::SimulationPlayerTarget& player)
    {
        if (player.hasBaseInfo != state.hasBaseInfo || player.hasClass != state.hasClass)
            return true;

        if (player.hasClass && player.classId != state.classId)
            return true;

        if (!player.hasBaseInfo)
            return false;

        return player.npc.mName != state.npc.mName
            || player.npc.mModel != state.npc.mModel
            || player.npc.mRace != state.npc.mRace
            || player.npc.mHair != state.npc.mHair
            || player.npc.mHead != state.npc.mHead
            || player.npc.mFlags != state.npc.mFlags
            || player.npc.mClass != state.npc.mClass;
    }

    void copyServerSimulationEquipment(mwmp::BaseActor& actor, const MWWorld::Ptr& ptr)
    {
        if (!ptr.getClass().hasInventoryStore(ptr))
            return;

        MWWorld::InventoryStore& inventoryStore = ptr.getClass().getInventoryStore(ptr);
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots && slot < mwmp::equipmentSlotCount; ++slot)
        {
            MWWorld::ContainerStoreIterator it = inventoryStore.getSlot(slot);
            mwmp::Item& item = actor.equipmentItems[slot];
            if (it == inventoryStore.end())
            {
                item.refId.clear();
                item.count = 0;
                item.charge = -1;
                item.enchantmentCharge = -1.f;
                item.soul.clear();
                continue;
            }

            item.refId = it->getCellRef().getRefId().serializeText();
            item.count = it->getCellRef().getCount();
            item.charge = it->getCellRef().getCharge();
            item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
            item.soul = it->getCellRef().getSoul().serializeText();
        }

        actor.hasEquipmentData = true;
    }

    std::string makeServerSimulationActorIdentityKey(
        const ESM::Cell& cell, std::string_view actorRefId, unsigned int actorRefNum, unsigned int actorMpNum)
    {
        std::string key = cell.getDescription();
        key.push_back('|');
        key.append(actorRefId);
        key.push_back('|');
        key += std::to_string(actorRefNum);
        key.push_back('|');
        key += std::to_string(actorMpNum);
        return key;
    }

    bool isSameServerSimulationCell(const ESM::Cell& left, const ESM::Cell& right)
    {
        if (left.isExterior() != right.isExterior())
            return false;

        if (left.isExterior())
            return left.mData.mX == right.mData.mX && left.mData.mY == right.mData.mY;

        return left.mName == right.mName;
    }

    bool hasFiniteServerSimulationPosition(const ESM::Position& position)
    {
        return std::isfinite(position.pos[0]) && std::isfinite(position.pos[1]) && std::isfinite(position.pos[2]);
    }

    float getServerSimulationHorizontalDistanceSquared(
        const ESM::Position& left, const ESM::Position& right)
    {
        const float dx = left.pos[0] - right.pos[0];
        const float dy = left.pos[1] - right.pos[1];
        return dx * dx + dy * dy;
    }

    mwmp::Target makeServerSimulationPlayerTargetFromActorState(
        mwmp::PacketGuid guid, const OMW::ServerSimulationPlayerActorState& state)
    {
        mwmp::Target target;
        target.guid = mwmp::unassignedPacketGuid();
        if (!mwmp::isPacketGuidAssigned(guid))
            return target;

        target.isPlayer = true;
        target.guid = guid;
        target.name = state.name;
        return target;
    }

    mwmp::Target makeServerSimulationPlayerTarget(const std::map<std::string, mwmp::Target>& actorPlayerTargets,
        const std::map<mwmp::PacketGuid, OMW::ServerSimulationPlayerActorState>& playerActors,
        const std::string& actorKey, const ESM::Cell& actorCell, const ESM::Position& actorPosition,
        mwmp::PacketGuid focusPlayerGuid, std::string_view focusPlayerName)
    {
        if (!actorKey.empty())
        {
            const auto cachedTarget = actorPlayerTargets.find(actorKey);
            if (cachedTarget != actorPlayerTargets.end() && cachedTarget->second.isPlayer
                && mwmp::isPacketGuidAssigned(cachedTarget->second.guid))
            {
                if (playerActors.empty())
                    return cachedTarget->second;

                const auto playerActor = playerActors.find(cachedTarget->second.guid);
                if (playerActor != playerActors.end()
                    && isSameServerSimulationCell(actorCell, playerActor->second.cell))
                    return cachedTarget->second;
            }
        }

        if (hasFiniteServerSimulationPosition(actorPosition))
        {
            std::optional<float> bestDistanceSquared;
            mwmp::Target bestTarget;
            bestTarget.guid = mwmp::unassignedPacketGuid();

            for (const auto& [guid, playerActor] : playerActors)
            {
                if (!mwmp::isPacketGuidAssigned(guid) || !hasFiniteServerSimulationPosition(playerActor.position)
                    || !isSameServerSimulationCell(actorCell, playerActor.cell))
                    continue;

                const float distanceSquared
                    = getServerSimulationHorizontalDistanceSquared(actorPosition, playerActor.position);
                if (!bestDistanceSquared || distanceSquared < *bestDistanceSquared)
                {
                    bestDistanceSquared = distanceSquared;
                    bestTarget = makeServerSimulationPlayerTargetFromActorState(guid, playerActor);
                }
            }

            if (bestDistanceSquared && bestTarget.isPlayer && mwmp::isPacketGuidAssigned(bestTarget.guid))
                return bestTarget;
        }

        mwmp::Target target;
        target.guid = mwmp::unassignedPacketGuid();
        if (!mwmp::isPacketGuidAssigned(focusPlayerGuid))
            return target;

        target.isPlayer = true;
        target.guid = focusPlayerGuid;
        target.name = std::string(focusPlayerName);
        return target;
    }

    mwmp::Target makeServerSimulationActorTarget(const MWWorld::Ptr& ptr)
    {
        mwmp::Target target;
        target.guid = mwmp::unassignedPacketGuid();

        if (ptr.isEmpty())
            return target;

        const ESM::RefNum refNum = ptr.getCellRef().getRefNum();
        if (refNum.mIndex == 0)
            return target;

        target.isPlayer = false;
        target.refId = ptr.getCellRef().getRefId().serializeText();
        target.refNum = refNum.mIndex;
        target.mpNum = 0;
        target.name = std::string(ptr.getClass().getName(ptr));
        return target;
    }

    bool setServerSimulationAiAction(mwmp::BaseActor& actor, MWMechanics::AiPackageTypeId typeId)
    {
        switch (typeId)
        {
            case MWMechanics::AiPackageTypeId::Wander:
                actor.aiAction = mwmp::BaseActorList::WANDER;
                return true;

            case MWMechanics::AiPackageTypeId::Travel:
            case MWMechanics::AiPackageTypeId::InternalTravel:
                actor.aiAction = mwmp::BaseActorList::TRAVEL;
                return true;

            case MWMechanics::AiPackageTypeId::Escort:
                actor.aiAction = mwmp::BaseActorList::ESCORT;
                return true;

            case MWMechanics::AiPackageTypeId::Follow:
                actor.aiAction = mwmp::BaseActorList::FOLLOW;
                return true;

            case MWMechanics::AiPackageTypeId::Activate:
                actor.aiAction = mwmp::BaseActorList::ACTIVATE;
                return true;

            case MWMechanics::AiPackageTypeId::Combat:
            case MWMechanics::AiPackageTypeId::Pursue:
                actor.aiAction = mwmp::BaseActorList::COMBAT;
                return true;

            case MWMechanics::AiPackageTypeId::None:
            case MWMechanics::AiPackageTypeId::AvoidDoor:
            case MWMechanics::AiPackageTypeId::Face:
            case MWMechanics::AiPackageTypeId::Breathe:
            case MWMechanics::AiPackageTypeId::Cast:
                return false;
        }

        return false;
    }

    void copyServerSimulationAi(mwmp::BaseActor& actor, const MWWorld::Ptr& ptr, const MWWorld::Ptr& player,
        std::map<std::string, mwmp::Target>& actorPlayerTargets,
        const std::map<mwmp::PacketGuid, OMW::ServerSimulationPlayerActorState>& playerActors,
        mwmp::PacketGuid focusPlayerGuid, std::string_view focusPlayerName)
    {
        const std::string actorKey = makeServerSimulationActorIdentityKey(
            actor.cell, actor.refId, actor.refNum, actor.mpNum);
        auto clearCachedPlayerTarget = [&] {
            if (!actorKey.empty())
                actorPlayerTargets.erase(actorKey);
        };

        const MWMechanics::AiSequence& aiSequence = ptr.getClass().getCreatureStats(ptr).getAiSequence();
        if (aiSequence.isEmpty())
        {
            clearCachedPlayerTarget();
            return;
        }

        const MWMechanics::AiPackage& package = aiSequence.getActivePackage();
        if (!setServerSimulationAiAction(actor, package.getTypeId()))
        {
            clearCachedPlayerTarget();
            return;
        }

        actor.hasAiData = true;
        actor.aiShouldRepeat = package.getRepeat();

        if (const std::optional<int> distance = package.getDistance())
            actor.aiDistance = static_cast<unsigned int>(std::max(0, *distance));

        if (const std::optional<float> duration = package.getDuration())
            actor.aiDuration = static_cast<unsigned int>(std::max(0.f, *duration));

        if (actor.aiAction == mwmp::BaseActorList::TRAVEL || actor.aiAction == mwmp::BaseActorList::ESCORT)
        {
            const osg::Vec3f destination = package.getDestination();
            actor.aiCoordinates.pos[0] = destination.x();
            actor.aiCoordinates.pos[1] = destination.y();
            actor.aiCoordinates.pos[2] = destination.z();
        }

        if (actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT
            || actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW)
        {
            const bool targetIsServerProxyPlayer = package.getTarget() == player;
            actor.aiTarget = targetIsServerProxyPlayer
                ? makeServerSimulationPlayerTarget(actorPlayerTargets, playerActors, actorKey, actor.cell,
                      actor.position, focusPlayerGuid, focusPlayerName)
                : makeServerSimulationActorTarget(package.getTarget());
            actor.hasAiTarget = actor.aiTarget.isPlayer || (actor.aiTarget.refNum != static_cast<unsigned int>(-1)
                && actor.aiTarget.mpNum != static_cast<unsigned int>(-1));
            if (!actorKey.empty())
            {
                if (targetIsServerProxyPlayer && actor.aiTarget.isPlayer
                    && mwmp::isPacketGuidAssigned(actor.aiTarget.guid))
                    actorPlayerTargets[actorKey] = actor.aiTarget;
                else
                    actorPlayerTargets.erase(actorKey);
            }
        }
        else
            clearCachedPlayerTarget();

        if ((actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT
                || actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW)
            && !actor.hasAiTarget)
        {
            actor.hasAiData = false;
        }
    }

    float sanitizeServerSimulationMovementComponent(float value)
    {
        constexpr float movementEpsilon = 0.0001f;
        if (!std::isfinite(value) || std::abs(value) <= movementEpsilon)
            return 0.f;

        return value;
    }

    bool isServerSimulationMeleeAttackType(std::string_view attackType)
    {
        return attackType == "chop" || attackType == "slash" || attackType == "thrust";
    }

    void copyServerSimulationAttack(mwmp::BaseActor& actor, const MWMechanics::CreatureStats& creatureStats)
    {
        const std::string_view attackType = creatureStats.getAttackType();
        actor.hasCombatData = true;
        actor.attack.type = static_cast<char>(mwmp::Attack::MELEE);
        actor.attack.pressed = creatureStats.getAttackingOrSpell();
        actor.attack.success = false;
        actor.attack.isHit = false;
        actor.attack.block = false;
        actor.attack.damage = 0.f;
        actor.attack.attackStrength = actor.attack.pressed ? 0.f : 1.f;

        if (isServerSimulationMeleeAttackType(attackType))
            actor.attack.attackAnimation = std::string(attackType);

        if (actor.hasAiTarget)
            actor.attack.target = actor.aiTarget;
    }

    bool appendServerSimulationActor(mwmp::BaseActorList& actorList, const MWWorld::Ptr& ptr,
        const MWWorld::Ptr& player, std::map<std::string, mwmp::Target>& actorPlayerTargets,
        const std::map<mwmp::PacketGuid, OMW::ServerSimulationPlayerActorState>& playerActors,
        mwmp::PacketGuid focusPlayerGuid, std::string_view focusPlayerName)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return true;

        const ESM::RefNum refNum = ptr.getCellRef().getRefNum();
        if (refNum.mIndex == 0)
            return true;

        mwmp::BaseActor actor;
        actor.refId = ptr.getCellRef().getRefId().serializeText();
        actor.refNum = refNum.mIndex;
        actor.mpNum = 0;
        actor.cell = actorList.cell;

        actor.position = ptr.getRefData().getPosition();
        const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
        for (int axis = 0; axis < 3; ++axis)
        {
            actor.direction.pos[axis] = sanitizeServerSimulationMovementComponent(movement.mPosition[axis]);
            actor.direction.rot[axis] = sanitizeServerSimulationMovementComponent(movement.mRotation[axis]);
        }
        actor.hasPositionData = true;
        actor.movementSampleIntervalSeconds = 1.f / 30.f;
        actor.movementLatencySeconds = 0.f;

        const MWMechanics::CreatureStats& creatureStats = ptr.getClass().getCreatureStats(ptr);
        if (creatureStats.isDead())
        {
            const std::string actorKey = makeServerSimulationActorIdentityKey(
                actorList.cell, actor.refId, actor.refNum, actor.mpNum);
            if (!actorKey.empty())
                actorPlayerTargets.erase(actorKey);
        }

        actor.creatureStats.mDead = creatureStats.isDead();
        actor.creatureStats.mDeathAnimationFinished = creatureStats.isDeathAnimationFinished();
        for (int i = 0; i < 3; ++i)
            copyDynamicStat(creatureStats.getDynamic(i), actor.creatureStats.mDynamic[i]);
        actor.hasStatsDynamicData = true;

        actor.drawState = static_cast<char>(static_cast<int>(creatureStats.getDrawState()));
        actor.movementFlags = 0;
        if (creatureStats.getMovementFlag(MWMechanics::CreatureStats::Flag_Sneak))
            actor.movementFlags |= MWMechanics::CreatureStats::Flag_Sneak;
        if (creatureStats.getMovementFlag(MWMechanics::CreatureStats::Flag_Run))
            actor.movementFlags |= MWMechanics::CreatureStats::Flag_Run;
        if (creatureStats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceJump))
            actor.movementFlags |= MWMechanics::CreatureStats::Flag_ForceJump;
        if (creatureStats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceMoveJump))
            actor.movementFlags |= MWMechanics::CreatureStats::Flag_ForceMoveJump;
        actor.isJumping = creatureStats.getMovementFlag(MWMechanics::CreatureStats::Flag_ForceJump);
        actor.isFlying = MWBase::Environment::get().getWorld()->isFlying(ptr);
        actor.hasAnimFlagsData = true;

        copyServerSimulationEquipment(actor, ptr);
        copyServerSimulationAi(actor, ptr, player, actorPlayerTargets, playerActors, focusPlayerGuid, focusPlayerName);
        copyServerSimulationAttack(actor, creatureStats);

        actorList.baseActors.push_back(std::move(actor));
        return true;
    }

    bool matchesServerSimulationActor(
        const MWWorld::Ptr& ptr, std::string_view actorRefId, unsigned int actorRefNum, unsigned int actorMpNum)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor() || ptr.getCellRef().getCount(false) == 0)
            return false;

        if (actorMpNum != 0)
            return false;

        const ESM::RefNum refNum = ptr.getCellRef().getRefNum();
        if (refNum.mIndex != actorRefNum)
            return false;

        return actorRefId.empty() || ptr.getCellRef().getRefId().serializeText() == actorRefId;
    }

    MWWorld::Ptr findServerSimulationActor(
        MWWorld::CellStore* cellStore, std::string_view actorRefId, unsigned int actorRefNum, unsigned int actorMpNum)
    {
        MWWorld::Ptr found;
        if (cellStore == nullptr)
            return found;

        cellStore->forEachType<ESM::NPC>([&](const MWWorld::Ptr& ptr) {
            if (!matchesServerSimulationActor(ptr, actorRefId, actorRefNum, actorMpNum))
                return true;

            found = ptr;
            return false;
        }, true);

        if (!found.isEmpty())
            return found;

        cellStore->forEachType<ESM::Creature>([&](const MWWorld::Ptr& ptr) {
            if (!matchesServerSimulationActor(ptr, actorRefId, actorRefNum, actorMpNum))
                return true;

            found = ptr;
            return false;
        }, true);

        return found;
    }

}

void OMW::Engine::executeLocalScripts()
{
    MWWorld::LocalScripts& localScripts = mWorld->getLocalScripts();

    localScripts.startIteration();
    std::pair<ESM::RefId, MWWorld::Ptr> script;
    while (localScripts.getNext(script))
    {
        MWScript::InterpreterContext interpreterContext(&script.second.getRefData().getLocals(), script.second);
        const std::string scriptName = script.first.serializeText();
        if (mwmp::Main::isInitialized() && mwmp::Main::isValidPacketScript(scriptName))
            interpreterContext.sendPackets = true;
        interpreterContext.trackContextType(ScriptController::ScriptLocal);
        interpreterContext.trackCurrentScriptName(scriptName);
        mScriptManager->run(script.first, interpreterContext);
    }
}

void OMW::Engine::hideServerSimulationWindow()
{
    if (!mServerSimulationMode || mWindow == nullptr)
        return;

    SDL_SetWindowTitle(mWindow, "CommunityMP Server Simulation");
    SDL_SetWindowData(mWindow, "OpenMW.ServerSimulationHidden", mWindow);
    SDL_HideWindow(mWindow);
}

void OMW::Engine::neutralizeServerSimulationPlayer()
{
    hideServerSimulationWindow();

    if (!mServerSimulationMode || mWorld == nullptr || mMechanicsManager == nullptr || mStateManager == nullptr
        || mStateManager->getState() != MWBase::StateManager::State_Running)
        return;

    MWWorld::Ptr player = mWorld->getPlayerPtr();
    if (player.isEmpty() || !player.getClass().isActor())
        return;

    const bool hasFocusPlayer = mServerSimulationFocusPlayerSet
        && mwmp::isPacketGuidAssigned(mServerSimulationFocusPlayerGuid);

    if (!mWorld->getGodModeState())
        static_cast<void>(mWorld->toggleGodMode());

    MWMechanics::CreatureStats& playerStats = player.getClass().getCreatureStats(player);
    playerStats.getAiSequence().clear();
    playerStats.setHitAttemptActor({});
    playerStats.setDrawState(MWMechanics::DrawState::Nothing);

    MWMechanics::Movement& playerMovement = player.getClass().getMovementSettings(player);
    for (int axis = 0; axis < 3; ++axis)
    {
        playerMovement.mPosition[axis] = 0.f;
        playerMovement.mRotation[axis] = 0.f;
    }
    playerMovement.mSpeedFactor = 0.f;
    mWorld->setActorCollisionMode(player, false, false);

    if (hasFocusPlayer)
        return;

    const std::vector<MWWorld::Ptr> serverFocusDummyTargets{ player };
    for (const MWWorld::Ptr& actor : mMechanicsManager->getActorsFighting(player))
    {
        if (actor.isEmpty() || actor == player || !actor.getClass().isActor())
            continue;

        MWMechanics::CreatureStats& actorStats = actor.getClass().getCreatureStats(actor);
        actorStats.getAiSequence().stopCombat(serverFocusDummyTargets);
        actorStats.setHitAttemptActor({});
    }
}

void OMW::Engine::applyServerSimulationFocusPlayerIdentity(
    std::string_view playerName, const ESM::NPC* playerNpc, const ESM::RefId* playerClassId)
{
    if (!mServerSimulationMode || mWorld == nullptr || mMechanicsManager == nullptr || mStateManager == nullptr
        || mStateManager->getState() != MWBase::StateManager::State_Running)
        return;

    MWWorld::Ptr player = mWorld->getPlayerPtr();
    if (player.isEmpty() || player.get<ESM::NPC>() == nullptr || player.get<ESM::NPC>()->mBase == nullptr)
        return;

    const ESM::NPC current = *player.get<ESM::NPC>()->mBase;
    if (!playerName.empty() && current.mName != playerName)
        mMechanicsManager->setPlayerName(std::string(playerName));

    if (playerNpc != nullptr)
    {
        const ESM::RefId& race = playerNpc->mRace;
        if (!race.empty() && mWorld->getStore().get<ESM::Race>().search(race) != nullptr
            && (current.mRace != race || current.mHead != playerNpc->mHead || current.mHair != playerNpc->mHair
                || current.isMale() != playerNpc->isMale()))
            mMechanicsManager->setPlayerRace(race, playerNpc->isMale(), playerNpc->mHead, playerNpc->mHair);
    }

    ESM::RefId classId;
    if (playerClassId != nullptr && !playerClassId->empty())
        classId = *playerClassId;
    else if (playerNpc != nullptr && !playerNpc->mClass.empty())
        classId = playerNpc->mClass;

    if (!classId.empty() && current.mClass != classId
        && mWorld->getStore().get<ESM::Class>().search(classId) != nullptr)
        mMechanicsManager->setPlayerClass(classId);
}

void OMW::Engine::applyServerSimulationActorBaseStats(
    const MWWorld::Ptr& actor, const mwmp::SimulationPlayerBaseStats& baseStats, std::string_view actorName)
{
    if (actor.isEmpty() || !actor.getClass().isActor())
        return;

    try
    {
        MWMechanics::CreatureStats& targetCreatureStats = actor.getClass().getCreatureStats(actor);
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            if (!isFiniteDynamicStatState(baseStats.attributes[i]))
                continue;

            MWMechanics::AttributeValue attributeValue
                = targetCreatureStats.getAttribute(ESM::Attribute::indexToRefId(i));
            attributeValue.readState(baseStats.attributes[i]);
            targetCreatureStats.setAttribute(ESM::Attribute::indexToRefId(i), attributeValue);
        }

        if (actor.get<ESM::NPC>() == nullptr)
            return;

        MWMechanics::NpcStats& targetNpcStats = actor.getClass().getNpcStats(actor);
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            if (!isFiniteDynamicStatState(baseStats.skills[i]))
                continue;

            MWMechanics::SkillValue skillValue = targetNpcStats.getSkill(ESM::Skill::indexToRefId(i));
            skillValue.readState(baseStats.skills[i]);
            targetNpcStats.setSkill(ESM::Skill::indexToRefId(i), skillValue);
        }
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "Failed to apply server simulation player base stats to "
                            << actorName << ": " << e.what();
    }
}

void OMW::Engine::applyServerSimulationFocusPlayerStats()
{
    if (!mServerSimulationMode || !mServerSimulationFocusPlayerSet || !mServerSimulationFocusPlayerStatsSet
        || mWorld == nullptr || mStateManager == nullptr
        || mStateManager->getState() != MWBase::StateManager::State_Running)
        return;

    MWWorld::Ptr player = mWorld->getPlayerPtr();
    if (player.isEmpty() || !player.getClass().isActor())
        return;

    const bool sourceDead = mServerSimulationFocusPlayerStats.mDead
        || mServerSimulationFocusPlayerStats.mDynamic[0].mCurrent <= 0.001f;

    MWMechanics::CreatureStats& playerStats = player.getClass().getCreatureStats(player);
    if (!sourceDead && playerStats.isDead())
        playerStats.resurrect();

    for (int i = 0; i < 3; ++i)
    {
        MWMechanics::DynamicStat<float> dynamicStat = playerStats.getDynamic(i);
        dynamicStat.readState(mServerSimulationFocusPlayerStats.mDynamic[i]);
        playerStats.setDynamic(i, dynamicStat);
    }

    playerStats.setDeathAnimationFinished(
        sourceDead && mServerSimulationFocusPlayerStats.mDeathAnimationFinished);
}

ESM::RefId OMW::Engine::ensureServerSimulationPlayerActorRecord(
    mwmp::PacketGuid guid, ServerSimulationPlayerActorState& state, const MWWorld::Ptr& proxyPlayer)
{
    if (!mwmp::isPacketGuidAssigned(guid) || proxyPlayer.isEmpty() || proxyPlayer.get<ESM::NPC>() == nullptr
        || proxyPlayer.get<ESM::NPC>()->mBase == nullptr || mWorld == nullptr)
        return ESM::RefId();

    if (state.recordId.empty())
        state.recordId = makeServerSimulationPlayerActorRecordId(guid);

    const ESM::NPC* proxyNpc = proxyPlayer.get<ESM::NPC>()->mBase;
    ESM::NPC record = *proxyNpc;
    if (state.hasBaseInfo)
    {
        record.mName = state.npc.mName;
        record.mModel = state.npc.mModel;
        record.mRace = state.npc.mRace;
        record.mHair = state.npc.mHair;
        record.mHead = state.npc.mHead;
        record.mFlags = state.npc.mFlags;
        if (!state.npc.mClass.empty())
            record.mClass = state.npc.mClass;
    }

    if (!state.name.empty())
        record.mName = state.name;

    if (state.hasClass && !state.classId.empty())
        record.mClass = state.classId;

    const MWWorld::ESMStore& store = mWorld->getStore();
    if (record.mRace.empty() || store.get<ESM::Race>().search(record.mRace) == nullptr)
        record.mRace = proxyNpc->mRace;
    if (record.mClass.empty() || store.get<ESM::Class>().search(record.mClass) == nullptr)
        record.mClass = proxyNpc->mClass;
    if (record.mName.empty())
        record.mName = proxyNpc->mName;

    record.mId = state.recordId;
    record.mInventory.mList.clear();
    record.mAiPackage.mList.clear();
    mWorld->getStore().overrideRecord(record);
    return record.mId;
}

void OMW::Engine::applyServerSimulationPlayerActorStats(ServerSimulationPlayerActorState& state)
{
    if (state.ptr.isEmpty() || !state.ptr.getClass().isActor() || !state.hasStatsDynamicData)
        return;

    const bool sourceDead = state.stats.mDead || state.stats.mDynamic[0].mCurrent <= 0.001f;

    MWMechanics::CreatureStats& stats = state.ptr.getClass().getCreatureStats(state.ptr);
    if (!sourceDead && stats.isDead())
        stats.resurrect();

    for (int i = 0; i < 3; ++i)
    {
        MWMechanics::DynamicStat<float> dynamicStat = stats.getDynamic(i);
        dynamicStat.readState(state.stats.mDynamic[i]);
        stats.setDynamic(i, dynamicStat);
    }

    stats.setDeathAnimationFinished(sourceDead && state.stats.mDeathAnimationFinished);
}

void OMW::Engine::applyServerSimulationEquipmentToActor(const MWWorld::Ptr& actor,
    const std::array<mwmp::Item, mwmp::equipmentSlotCount>& equipmentItems, std::string_view actorName)
{
    if (actor.isEmpty() || !actor.getClass().hasInventoryStore(actor))
        return;

    MWWorld::InventoryStore& inventoryStore = actor.getClass().getInventoryStore(actor);
    MWWorld::ContainerStore& containerStore = actor.getClass().getContainerStore(actor);
    for (int slot = 0; slot < MWWorld::InventoryStore::Slots && slot < mwmp::equipmentSlotCount; ++slot)
    {
        const mwmp::Item& item = equipmentItems[slot];
        if (!mwmp::isValidEquipmentItem(item))
            continue;

        const ESM::RefId desiredId = item.refId.empty() ? ESM::RefId() : ESM::RefId::stringRefId(item.refId);
        MWWorld::ContainerStoreIterator equipped = inventoryStore.getSlot(slot);
        if (equipped != inventoryStore.end())
        {
            const ESM::RefId equippedId = equipped->getCellRef().getRefId();
            if (equippedId == desiredId)
                continue;

            try
            {
                containerStore.remove(equippedId, containerStore.count(equippedId), false);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to remove stale server simulation player equipment "
                                    << equippedId << " from " << actorName << ": " << e.what();
            }
        }

        if (desiredId.empty())
            continue;

        try
        {
            if (MWBase::Environment::get().getESMStore()->find(desiredId) == 0)
                continue;

            MWWorld::ContainerStoreIterator added = containerStore.add(desiredId, item.count, false);
            if (added == containerStore.end())
                continue;

            added->getCellRef().setCharge(item.charge);
            added->getCellRef().setEnchantmentCharge(item.enchantmentCharge);
            added->getCellRef().setSoul(ESM::RefId::stringRefId(item.soul));
            std::shared_ptr<MWWorld::Action> action = added->getClass().use(*added);
            action->execute(actor, true);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to equip server simulation player item "
                                << item.refId << " on " << actorName << ": " << e.what();
        }
    }
}

void OMW::Engine::applyServerSimulationPlayerActorEquipment(ServerSimulationPlayerActorState& state)
{
    if (!state.hasEquipmentData)
        return;

    applyServerSimulationEquipmentToActor(state.ptr, state.equipmentItems, state.name);
}

void OMW::Engine::clearServerSimulationPlayerActorReference(ServerSimulationPlayerActorState& state)
{
    if (!state.ptr.isEmpty() && mWorld != nullptr)
    {
        try
        {
            mWorld->deleteObject(state.ptr);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to delete server simulation player actor reference for "
                                << state.name << ": " << e.what();
        }
    }

    state.ptr = MWWorld::Ptr();
    state.reference.reset();
}

void OMW::Engine::clearServerSimulationPlayerActorReferences()
{
    for (auto& [guid, state] : mServerSimulationPlayerActors)
    {
        static_cast<void>(guid);
        clearServerSimulationPlayerActorReference(state);
    }
}

bool OMW::Engine::isServerSimulationPlayerActorReference(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty())
        return false;

    for (const auto& [guid, state] : mServerSimulationPlayerActors)
    {
        static_cast<void>(guid);
        if (!state.ptr.isEmpty() && state.ptr == ptr)
            return true;
    }

    return false;
}

void OMW::Engine::syncServerSimulationPlayerActorReferences()
{
    if (!mServerSimulationMode || !mServerSimulationPrepared || mWorld == nullptr || mStateManager == nullptr
        || mStateManager->getState() != MWBase::StateManager::State_Running)
        return;

    MWWorld::Ptr proxyPlayer = mWorld->getPlayerPtr();
    if (proxyPlayer.isEmpty() || !proxyPlayer.getClass().isActor() || proxyPlayer.getCell() == nullptr
        || proxyPlayer.getCell()->getCell() == nullptr)
        return;

    MWWorld::CellStore* activeCellStore = proxyPlayer.getCell();
    const ESM::Cell activeCell = mwmp::makeActorPacketCell(*activeCellStore->getCell());
    const bool hasFocusPlayer = mServerSimulationFocusPlayerSet
        && mwmp::isPacketGuidAssigned(mServerSimulationFocusPlayerGuid);

    for (auto& [guid, state] : mServerSimulationPlayerActors)
    {
        const bool shouldHaveReference = mwmp::isPacketGuidAssigned(guid)
            && (!hasFocusPlayer || guid != mServerSimulationFocusPlayerGuid)
            && !state.cell.getDescription().empty()
            && isSameServerSimulationCell(activeCell, state.cell)
            && hasFiniteServerSimulationPosition(state.position);

        if (!shouldHaveReference)
        {
            clearServerSimulationPlayerActorReference(state);
            continue;
        }

        bool needsNewReference = state.reference == nullptr || state.ptr.isEmpty()
            || state.ptr.getCell() == nullptr || state.ptr.getCell()->getCell() == nullptr
            || state.ptr.getCell() != activeCellStore;
        if (!needsNewReference)
        {
            const ESM::Cell referenceCell = mwmp::makeActorPacketCell(*state.ptr.getCell()->getCell());
            needsNewReference = !isSameServerSimulationCell(referenceCell, activeCell);
        }

        if (needsNewReference)
        {
            clearServerSimulationPlayerActorReference(state);
            try
            {
                ESM::RefId playerRecordId = ensureServerSimulationPlayerActorRecord(guid, state, proxyPlayer);
                if (playerRecordId.empty())
                    playerRecordId = proxyPlayer.getCellRef().getRefId();
                state.reference = std::make_unique<MWWorld::ManualRef>(
                    *MWBase::Environment::get().getESMStore(), playerRecordId, 1);
                state.ptr = mWorld->placeObject(state.reference->getPtr(), activeCellStore, state.position);
                Log(Debug::Verbose) << "Created server simulation player actor reference for "
                                    << state.name << " in " << activeCell.getDescription();
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to create server simulation player actor reference for "
                                    << state.name << " in " << activeCell.getDescription() << ": " << e.what();
                clearServerSimulationPlayerActorReference(state);
                continue;
            }
        }
        else
            state.ptr = mWorld->moveObject(state.ptr, activeCellStore, state.position.asVec3(), true, true);

        mWorld->rotateObject(state.ptr, state.position.asRotationVec3());

        MWMechanics::CreatureStats& stats = state.ptr.getClass().getCreatureStats(state.ptr);
        stats.getAiSequence().clear();
        stats.setHitAttemptActor({});
        stats.setDrawState(MWMechanics::DrawState::Nothing);
        MWMechanics::Movement& movement = state.ptr.getClass().getMovementSettings(state.ptr);
        for (int axis = 0; axis < 3; ++axis)
        {
            movement.mPosition[axis] = 0.f;
            movement.mRotation[axis] = 0.f;
        }
        movement.mSpeedFactor = 0.f;

        if (state.hasBaseStatsData)
            applyServerSimulationActorBaseStats(state.ptr, state.baseStats, state.name);
        applyServerSimulationPlayerActorStats(state);
        applyServerSimulationPlayerActorEquipment(state);
    }

    if (mMechanicsManager == nullptr)
        return;

    auto retargetActorCombat = [&](const MWWorld::Ptr& actor) {
        if (actor.isEmpty() || actor == proxyPlayer || isServerSimulationPlayerActorReference(actor)
            || !actor.getClass().isActor())
            return true;

        const ESM::RefNum refNum = actor.getCellRef().getRefNum();
        if (refNum.mIndex == 0)
            return true;

        const std::string actorKey = makeServerSimulationActorIdentityKey(
            activeCell, actor.getCellRef().getRefId().serializeText(), refNum.mIndex, 0);
        const auto targetIt = mServerSimulationActorPlayerTargets.find(actorKey);
        if (targetIt == mServerSimulationActorPlayerTargets.end() || !targetIt->second.isPlayer
            || !mwmp::isPacketGuidAssigned(targetIt->second.guid))
            return true;

        if (hasFocusPlayer && targetIt->second.guid == mServerSimulationFocusPlayerGuid)
            return true;

        const auto playerIt = mServerSimulationPlayerActors.find(targetIt->second.guid);
        if (playerIt == mServerSimulationPlayerActors.end() || playerIt->second.ptr.isEmpty()
            || playerIt->second.ptr.getCell() != activeCellStore)
            return true;

        MWMechanics::AiSequence& aiSequence = actor.getClass().getCreatureStats(actor).getAiSequence();
        MWWorld::Ptr currentTarget;
        if (aiSequence.getCombatTarget(currentTarget))
        {
            if (currentTarget == playerIt->second.ptr)
                return true;

            if (currentTarget != proxyPlayer)
                return true;
        }

        aiSequence.stopCombat();
        mMechanicsManager->startCombat(actor, playerIt->second.ptr, nullptr);
        return true;
    };

    activeCellStore->forEachType<ESM::NPC>(retargetActorCombat);
    activeCellStore->forEachType<ESM::Creature>(retargetActorCombat);
}

bool OMW::Engine::frame(unsigned frameNumber, float frametime)
{
    const osg::Timer_t frameStart = mViewer->getStartTick();
    const osg::Timer* const timer = osg::Timer::instance();
    osg::Stats* const stats = mViewer->getViewerStats();

    mEnvironment.setFrameDuration(frametime);

    try
    {
        // update input
        {
            ScopedProfile<UserStatsType::Input> profile(frameStart, frameNumber, *timer, *stats);
            bool disableControls = false;
#ifdef BUILD_TES3MP_CLIENT
            disableControls = mwmp::Main::isInitialized() && !mwmp::Main::get().getLocalPlayer()->isLoggedIn();
#endif
            mInputManager->update(frametime, disableControls);
        }

        // When the window is minimized, pause the game. Currently this *has* to be here to work around a MyGUI bug.
        // If we are not currently rendering, then RenderItems will not be reused resulting in a memory leak upon
        // changing widget textures (fixed in MyGUI 3.3.2), and destroyed widgets will not be deleted (not fixed yet,
        // https://github.com/MyGUI/mygui/issues/21)
        {
            ScopedProfile<UserStatsType::Sound> profile(frameStart, frameNumber, *timer, *stats);

            if (!mServerSimulationMode && !mWindowManager->isWindowVisible())
            {
                mSoundManager->pausePlayback();
                return false;
            }
            else if (!mServerSimulationMode)
                mSoundManager->resumePlayback();

            // sound
            if (mUseSound)
                mSoundManager->update(frametime);
        }

        {
            ScopedProfile<UserStatsType::LuaSyncUpdate> profile(frameStart, frameNumber, *timer, *stats);
            // Should be called after input manager update and before any change to the game world.
            // It applies to the game world queued changes from the previous frame.
            mLuaManager->synchronizedUpdate();
        }

        // update game state
        {
            ScopedProfile<UserStatsType::State> profile(frameStart, frameNumber, *timer, *stats);
            mStateManager->update(frametime);
        }

        bool paused = mWorld->getTimeManager()->isPaused();
#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::shouldRunWorldWhilePaused())
            paused = false;
#endif

        {
            ScopedProfile<UserStatsType::Script> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                if (!mWindowManager->containsMode(MWGui::GM_MainMenu) || !paused)
                {
                    if (mWorld->getScriptsEnabled())
                    {
                        // local scripts
                        executeLocalScripts();

                        // global scripts
                        mScriptManager->getGlobalScripts().run();
                    }

                    mWorld->getWorldScene().markCellAsUnchanged();
                }

                if (!paused)
                {
                    double hours = (frametime * mWorld->getTimeManager()->getGameTimeScale()) / 3600.0;
                    mWorld->advanceTime(hours, true);
                    mWorld->rechargeItems(frametime, true);
                }
            }
        }

        // update mechanics
        {
            ScopedProfile<UserStatsType::Mechanics> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                mMechanicsManager->update(frametime, paused);
            }

            if (mStateManager->getState() == MWBase::StateManager::State_Running)
            {
                MWWorld::Ptr player = mWorld->getPlayerPtr();
                if (!paused && player.getClass().getCreatureStats(player).isDead()
#ifdef BUILD_TES3MP_CLIENT
                    && !mwmp::Main::isInitialized()
#endif
                )
                    mStateManager->endGame();
            }
        }

        // update physics
        {
            ScopedProfile<UserStatsType::Physics> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                mWorld->updatePhysics(frametime, paused, frameStart, frameNumber, *stats);
            }
        }

        // update world
        {
            ScopedProfile<UserStatsType::World> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                mWorld->update(frametime, paused);
            }
        }

        // update GUI
        {
            ScopedProfile<UserStatsType::Gui> profile(frameStart, frameNumber, *timer, *stats);
            mWindowManager->update(frametime);
        }

#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::isInitialized())
            mwmp::Main::frame(frametime);
#endif
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "Error in frame: " << e.what();
    }

    const bool reportResource = stats->collectStats("resource");

    if (reportResource)
        stats->setAttribute(frameNumber, "UnrefQueue", static_cast<double>(mUnrefQueue->getSize()));

    mUnrefQueue->flush(*mWorkQueue);

    if (reportResource)
    {
        stats->setAttribute(frameNumber, "FrameNumber", frameNumber);

        mResourceSystem->reportStats(frameNumber, stats);

        stats->setAttribute(frameNumber, "WorkQueue", static_cast<double>(mWorkQueue->getNumItems()));
        stats->setAttribute(frameNumber, "WorkThread", static_cast<double>(mWorkQueue->getNumActiveThreads()));

        mMechanicsManager->reportStats(frameNumber, *stats);
        mWorld->reportStats(frameNumber, *stats);
        mLuaManager->reportStats(frameNumber, *stats);

        stats->setAttribute(frameNumber, "StringRefId Count", static_cast<double>(ESM::StringRefId::totalCount()));
    }

    // These viewer/focus updates do not execute Lua scripts, so a threaded build can spend this time on Lua GC.
    mLuaWorker->gc(frameStart, frameNumber, *stats);

    mStereoManager->updateSettings(Settings::camera().mNearClip, Settings::camera().mViewingDistance);

    mViewer->eventTraversal();
    mViewer->updateTraversal();

    // update focus object for GUI
    {
        ScopedProfile<UserStatsType::Focus> profile(frameStart, frameNumber, *timer, *stats);
        mWorld->updateFocusObject();
    }

    mLuaWorker->finishGc(frameStart, frameNumber, *stats);

    // if there is a separate Lua thread, it starts the update now
    mLuaWorker->allowUpdate(frameStart, frameNumber, *stats);

    mViewer->renderingTraversals();

    mLuaWorker->finishUpdate(frameStart, frameNumber, *stats);

    return true;
}

OMW::Engine::Engine(Files::ConfigurationManager& configurationManager)
    : mWindow(nullptr)
    , mEncoding(ToUTF8::WINDOWS_1252)
    , mScreenCaptureOperation(nullptr)
    , mSelectDepthFormatOperation(new SceneUtil::SelectDepthFormatOperation())
    , mSelectColorFormatOperation(new SceneUtil::Color::SelectColorFormatOperation())
    , mStereoManager(nullptr)
    , mSkipMenu(false)
    , mUseSound(true)
    , mCompileAll(false)
    , mCompileAllDialogue(false)
    , mWarningsMode(1)
    , mScriptConsoleMode(false)
    , mActivationDistanceOverride(-1)
    , mGrab(true)
    , mExportFonts(false)
    , mRandomSeed(0)
    , mNewGame(false)
    , mCfgMgr(configurationManager)
    , mGlMaxTextureImageUnits(0)
{
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_SetHint(SDL_HINT_MAC_OPENGL_ASYNC_DISPATCH, "1");
#endif
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0"); // We use only gamepads

    Uint32 flags
        = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR;
    if (SDL_WasInit(flags) == 0)
    {
        SDL_SetMainReady();
        if (SDL_Init(flags) != 0)
        {
            throw std::runtime_error("Could not initialize SDL! " + std::string(SDL_GetError()));
        }
    }
}

OMW::Engine::~Engine()
{
    clearServerSimulationPlayerActorReferences();

    if (mServerSimulationMode)
        OMW::setServerSimulationModeActive(false);

#ifdef BUILD_TES3MP_CLIENT
    if (mwmp::Main::isInitialized())
    {
        mwmp::Main::get().getGUIController()->cleanUp();
        mwmp::Main::destroy();
    }
#endif

    if (mScreenCaptureOperation != nullptr)
    {
        mScreenCaptureOperation->stop();
        mScreenCaptureOperation = nullptr;
    }
    mScreenCaptureHandler = nullptr;

    mMechanicsManager = nullptr;
    mDialogueManager = nullptr;
    mJournal = nullptr;
    mWindowManager = nullptr;
    mScriptManager = nullptr;
    mWorld = nullptr;
    mStereoManager = nullptr;
    mSoundManager = nullptr;
    mInputManager = nullptr;
    mStateManager = nullptr;
    mLuaWorker = nullptr;
    mLuaManager = nullptr;
    mL10nManager = nullptr;

    mScriptContext = nullptr;

    mUnrefQueue = nullptr;
    mWorkQueue = nullptr;

    mViewer = nullptr;

    mResourceSystem.reset();

    mEncoder = nullptr;

    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }

    SDL_Quit();

    Log(Debug::Info) << "Quitting peacefully.";
}

// Set data dir

void OMW::Engine::setDataDirs(const Files::PathContainer& dataDirs)
{
    mDataDirs = dataDirs;
    mDataDirs.insert(mDataDirs.begin(), mResDir / "vfs");
    mFileCollections = Files::Collections(mDataDirs);
}

// Add BSA archive
void OMW::Engine::addArchive(const std::string& archive)
{
    mArchives.push_back(archive);
}

// Set resource dir
void OMW::Engine::setResourceDir(const std::filesystem::path& parResDir)
{
    mResDir = parResDir;
    if (!Version::checkResourcesVersion(mResDir))
        Log(Debug::Error) << "Resources dir " << mResDir
                          << " doesn't match OpenMW binary, the game may work incorrectly.";
}

// Set start cell name
void OMW::Engine::setCell(const std::string& cellName)
{
    mCellName = cellName;
}

void OMW::Engine::addContentFile(const std::string& file)
{
    mContentFiles.push_back(file);
}

void OMW::Engine::addGroundcoverFile(const std::string& file)
{
    mGroundcoverFiles.emplace_back(file);
}

void OMW::Engine::setSkipMenu(bool skipMenu, bool newGame)
{
    mSkipMenu = skipMenu;
    mNewGame = newGame;
}

void OMW::Engine::createWindow()
{
    const int screen = Settings::video().mScreen;
    const bool hiddenServerSimulationWindow = mServerSimulationMode;
    const int width = hiddenServerSimulationWindow ? 64 : Settings::video().mResolutionX;
    const int height = hiddenServerSimulationWindow ? 64 : Settings::video().mResolutionY;
    const Settings::WindowMode windowMode = Settings::video().mWindowMode;
    const bool windowBorder = !hiddenServerSimulationWindow && Settings::video().mWindowBorder;
    const SDLUtil::VSyncMode vsync = Settings::video().mVsyncMode;
    unsigned antialiasing = static_cast<unsigned>(Settings::video().mAntialiasing);

    int posX = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);
    int posY = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);

    if (windowMode == Settings::WindowMode::Fullscreen || windowMode == Settings::WindowMode::WindowedFullscreen)
    {
        posX = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
        posY = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
    }

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
    if (hiddenServerSimulationWindow)
        flags |= SDL_WINDOW_HIDDEN;
    else
        flags |= SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

    if (!hiddenServerSimulationWindow && windowMode == Settings::WindowMode::Fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    else if (!hiddenServerSimulationWindow && windowMode == Settings::WindowMode::WindowedFullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    // Allows for Windows snapping features to properly work in borderless window
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
    SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");

    if (!windowBorder)
        flags |= SDL_WINDOW_BORDERLESS;

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, Settings::video().mMinimizeOnFocusLoss ? "1" : "0");

    checkSDLError(SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24));
    if (Debug::shouldDebugOpenGL())
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG));

    if (antialiasing > 0)
    {
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1));
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
    }

    osg::ref_ptr<SDLUtil::GraphicsWindowSDL2> graphicsWindow;
    while (!graphicsWindow || !graphicsWindow->valid())
    {
        while (!mWindow)
        {
#ifdef BUILD_TES3MP_CLIENT
            const char* windowTitle
                = hiddenServerSimulationWindow ? "CommunityMP Server Simulation" : mwmp::Branding::productName;
#else
            const char* windowTitle = hiddenServerSimulationWindow ? "CommunityMP Server Simulation" : "OpenMW";
#endif
            mWindow = SDL_CreateWindow(
                windowTitle,
                posX, posY, width, height, flags);
            if (mWindow != nullptr && hiddenServerSimulationWindow)
                hideServerSimulationWindow();
            if (!mWindow)
            {
                // Try with a lower AA
                if (antialiasing > 0)
                {
                    Log(Debug::Warning) << "Warning: " << antialiasing << "x antialiasing not supported, trying "
                                        << antialiasing / 2;
                    antialiasing /= 2;
                    Settings::video().mAntialiasing.set(antialiasing);
                    checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
                    continue;
                }
                else
                {
                    std::stringstream error;
                    error << "Failed to create SDL window: " << SDL_GetError();
                    throw std::runtime_error(error.str());
                }
            }
        }

        // Since we use physical resolution internally, we have to create the window with scaled resolution,
        // but we can't get the scale before the window exists, so instead we have to resize aftewards.
        int w, h;
        SDL_GetWindowSize(mWindow, &w, &h);
        int dw, dh;
        SDL_GL_GetDrawableSize(mWindow, &dw, &dh);
        if (dw != w || dh != h)
        {
            SDL_SetWindowSize(mWindow, width / (dw / w), height / (dh / h));
        }

        if (!hiddenServerSimulationWindow)
            setWindowIcon();

        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        SDL_GetWindowPosition(mWindow, &traits->x, &traits->y);
        SDL_GL_GetDrawableSize(mWindow, &traits->width, &traits->height);
        traits->windowName = SDL_GetWindowTitle(mWindow);
        traits->windowDecoration = !(SDL_GetWindowFlags(mWindow) & SDL_WINDOW_BORDERLESS);
        traits->screenNum = SDL_GetWindowDisplayIndex(mWindow);
        traits->vsync = 0;
        traits->inheritedWindowData = new SDLUtil::GraphicsWindowSDL2::WindowData(mWindow);

        graphicsWindow = new SDLUtil::GraphicsWindowSDL2(traits, vsync);
        if (!graphicsWindow->valid())
            throw std::runtime_error("Failed to create GraphicsContext");

        if (traits->samples < antialiasing)
        {
            Log(Debug::Warning) << "Warning: Framebuffer MSAA level is only " << traits->samples << "x instead of "
                                << antialiasing << "x. Trying " << antialiasing / 2 << "x instead.";
            graphicsWindow->closeImplementation();
            SDL_DestroyWindow(mWindow);
            mWindow = nullptr;
            antialiasing /= 2;
            Settings::video().mAntialiasing.set(antialiasing);
            checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
            continue;
        }

        if (traits->red < 8)
            Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->red << " bit red channel.";
        if (traits->green < 8)
            Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->green << " bit green channel.";
        if (traits->blue < 8)
            Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->blue << " bit blue channel.";
        if (traits->depth < 24)
            Log(Debug::Warning) << "Warning: Framebuffer only has " << traits->depth << " bits of depth precision.";

        traits->alpha = 0; // set to 0 to stop ScreenCaptureHandler reading the alpha channel
    }

    osg::ref_ptr<osg::Camera> camera = mViewer->getCamera();
    camera->setGraphicsContext(graphicsWindow);
    camera->setViewport(0, 0, graphicsWindow->getTraits()->width, graphicsWindow->getTraits()->height);

    osg::ref_ptr<SceneUtil::OperationSequence> realizeOperations = new SceneUtil::OperationSequence(false);
    mViewer->setRealizeOperation(realizeOperations);
    osg::ref_ptr<IdentifyOpenGLOperation> identifyOp = new IdentifyOpenGLOperation();
    realizeOperations->add(identifyOp);
    realizeOperations->add(new SceneUtil::GetGLExtensionsOperation());

    if (Debug::shouldDebugOpenGL())
        realizeOperations->add(new Debug::EnableGLDebugOperation());

    realizeOperations->add(mSelectDepthFormatOperation);
    realizeOperations->add(mSelectColorFormatOperation);

    if (Stereo::getStereo())
    {
        Stereo::Settings settings;

        settings.mMultiview = Settings::stereo().mMultiview;
        settings.mAllowDisplayListsForMultiview = Settings::stereo().mAllowDisplayListsForMultiview;
        settings.mSharedShadowMaps = Settings::stereo().mSharedShadowMaps;

        if (Settings::stereo().mUseCustomView)
        {
            const osg::Vec3 leftEyeOffset(Settings::stereoView().mLeftEyeOffsetX,
                Settings::stereoView().mLeftEyeOffsetY, Settings::stereoView().mLeftEyeOffsetZ);

            const osg::Quat leftEyeOrientation(Settings::stereoView().mLeftEyeOrientationX,
                Settings::stereoView().mLeftEyeOrientationY, Settings::stereoView().mLeftEyeOrientationZ,
                Settings::stereoView().mLeftEyeOrientationW);

            const osg::Vec3 rightEyeOffset(Settings::stereoView().mRightEyeOffsetX,
                Settings::stereoView().mRightEyeOffsetY, Settings::stereoView().mRightEyeOffsetZ);

            const osg::Quat rightEyeOrientation(Settings::stereoView().mRightEyeOrientationX,
                Settings::stereoView().mRightEyeOrientationY, Settings::stereoView().mRightEyeOrientationZ,
                Settings::stereoView().mRightEyeOrientationW);

            settings.mCustomView = Stereo::CustomView{
                .mLeft = Stereo::View{
                    .pose = Stereo::Pose{
                        .position = leftEyeOffset,
                        .orientation = leftEyeOrientation,
                    },
                    .fov = Stereo::FieldOfView{
                        .angleLeft = Settings::stereoView().mLeftEyeFovLeft,
                        .angleRight = Settings::stereoView().mLeftEyeFovRight,
                        .angleUp = Settings::stereoView().mLeftEyeFovUp,
                        .angleDown = Settings::stereoView().mLeftEyeFovDown,
                    },
                },
                .mRight = Stereo::View{
                    .pose = Stereo::Pose{
                        .position = rightEyeOffset,
                        .orientation = rightEyeOrientation,
                    },
                    .fov = Stereo::FieldOfView{
                        .angleLeft = Settings::stereoView().mRightEyeFovLeft,
                        .angleRight = Settings::stereoView().mRightEyeFovRight,
                        .angleUp = Settings::stereoView().mRightEyeFovUp,
                        .angleDown = Settings::stereoView().mRightEyeFovDown,
                    },
                },
            };
        }

        if (Settings::stereo().mUseCustomEyeResolution)
            settings.mEyeResolution
                = osg::Vec2i(Settings::stereoView().mEyeResolutionX, Settings::stereoView().mEyeResolutionY);

        realizeOperations->add(new Stereo::InitializeStereoOperation(settings));
    }

    mViewer->realize();
    if (hiddenServerSimulationWindow)
        hideServerSimulationWindow();
    mGlMaxTextureImageUnits = identifyOp->getMaxTextureImageUnits();

    mViewer->getEventQueue()->getCurrentEventState()->setWindowRectangle(
        0, 0, graphicsWindow->getTraits()->width, graphicsWindow->getTraits()->height);
}

void OMW::Engine::setWindowIcon()
{
    std::ifstream windowIconStream;
#ifdef BUILD_TES3MP_CLIENT
    const auto windowIcon = mResDir / "tes3mp_logo.png";
#else
    const auto windowIcon = mResDir / "openmw.png";
#endif
    windowIconStream.open(windowIcon, std::ios_base::in | std::ios_base::binary);
    if (windowIconStream.fail())
        Log(Debug::Error) << "Error: Failed to open " << windowIcon;
    osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension("png");
    if (!reader)
    {
        Log(Debug::Error) << "Error: Failed to read window icon, no png readerwriter found";
        return;
    }
    osgDB::ReaderWriter::ReadResult result = reader->readImage(windowIconStream);
    if (!result.success())
        Log(Debug::Error) << "Error: Failed to read " << windowIcon << ": " << result.message() << " code "
                          << result.status();
    else
    {
        osg::ref_ptr<osg::Image> image = result.getImage();
        auto surface = SDLUtil::imageToSurface(image, true);
        SDL_SetWindowIcon(mWindow, surface.get());
    }
}

void OMW::Engine::prepareEngine()
{
    const std::filesystem::path savesPath = mServerSimulationMode && !mServerSimulationSavesPath.empty()
        ? mServerSimulationSavesPath
        : mCfgMgr.getUserDataPath() / "saves";
    mStateManager = std::make_unique<MWState::StateManager>(savesPath, mContentFiles);
    mEnvironment.setStateManager(*mStateManager);

    const bool stereoEnabled = Settings::stereo().mStereoEnabled || osg::DisplaySettings::instance().get()->getStereo();
    mStereoManager = std::make_unique<Stereo::Manager>(
        mViewer, stereoEnabled, Settings::camera().mNearClip, Settings::camera().mViewingDistance);

    osg::ref_ptr<osg::Group> rootNode(new osg::Group);
    mViewer->setSceneData(rootNode);

    createWindow();

    mVFS = std::make_unique<VFS::Manager>();

    VFS::registerArchives(mVFS.get(), mFileCollections, mArchives, true, &mEncoder.get()->getStatelessEncoder());

    mResourceSystem = std::make_unique<Resource::ResourceSystem>(
        mVFS.get(), Settings::cells().mCacheExpiryDelay, &mEncoder.get()->getStatelessEncoder());
    mResourceSystem->getSceneManager()->getShaderManager().setMaxTextureUnits(mGlMaxTextureImageUnits);
    mResourceSystem->getSceneManager()->setUnRefImageDataAfterApply(
        false); // keep to Off for now to allow better state sharing
    mResourceSystem->getSceneManager()->setFilterSettings(Settings::general().mTextureMagFilter,
        Settings::general().mTextureMinFilter, Settings::general().mTextureMipmap,
        static_cast<float>(Settings::general().mAnisotropy));
    mEnvironment.setResourceSystem(*mResourceSystem);

    mWorkQueue = new SceneUtil::WorkQueue(Settings::cells().mPreloadNumThreads);
    mUnrefQueue = std::make_unique<SceneUtil::UnrefQueue>();

    mScreenCaptureOperation = new SceneUtil::AsyncScreenCaptureOperation(mWorkQueue,
        new SceneUtil::WriteScreenshotToFileOperation(mCfgMgr.getScreenshotPath(),
            Settings::general().mScreenshotFormat,
            Settings::general().mNotifyOnSavedScreenshot ? std::function<void(std::string)>(ScreenCaptureMessageBox{})
                                                         : std::function<void(std::string)>(IgnoreString{})));

    mScreenCaptureHandler = new osgViewer::ScreenCaptureHandler(mScreenCaptureOperation);

    mViewer->addEventHandler(mScreenCaptureHandler);

    mL10nManager = std::make_unique<L10n::Manager>(mVFS.get());
    mL10nManager->setPreferredLocales(Settings::general().mPreferredLocales, Settings::general().mGmstOverridesL10n);
    mEnvironment.setL10nManager(*mL10nManager);

    mLuaManager = std::make_unique<MWLua::LuaManager>(mVFS.get(), mResDir / "lua_libs");
    mEnvironment.setLuaManager(*mLuaManager);

    // Create input and UI first to set up a bootstrapping environment for
    // showing a loading screen and keeping the window responsive while doing so

    const auto keybinderUser = mCfgMgr.getUserConfigPath() / "input_v3.xml";
    bool keybinderUserExists = std::filesystem::exists(keybinderUser);
    if (!keybinderUserExists)
    {
        const auto input2 = (mCfgMgr.getUserConfigPath() / "input_v2.xml");
        if (std::filesystem::exists(input2))
        {
            keybinderUserExists = std::filesystem::copy_file(input2, keybinderUser);
            Log(Debug::Info) << "Loading keybindings file: " << keybinderUser;
        }
    }
    else
        Log(Debug::Info) << "Loading keybindings file: " << keybinderUser;

    const auto userdefault = mCfgMgr.getUserConfigPath() / "gamecontrollerdb.txt";
    const auto localdefault = mCfgMgr.getLocalPath() / "gamecontrollerdb.txt";

    std::filesystem::path userGameControllerdb;
    if (std::filesystem::exists(userdefault))
        userGameControllerdb = userdefault;

    std::filesystem::path gameControllerdb;
    if (std::filesystem::exists(localdefault))
        gameControllerdb = localdefault;
    else if (!mCfgMgr.getGlobalPath().empty())
    {
        const auto globaldefault = mCfgMgr.getGlobalPath() / "gamecontrollerdb.txt";
        if (std::filesystem::exists(globaldefault))
            gameControllerdb = globaldefault;
    }
    // else if it doesn't exist, pass in an empty path

    // gui needs our shaders path before everything else
    mResourceSystem->getSceneManager()->setShaderPath(mResDir / "shaders");

    osg::GLExtensions& exts = SceneUtil::getGLExtensions();

#if OSG_VERSION_LESS_THAN(3, 6, 6)
    // hack fix for https://github.com/openscenegraph/OpenSceneGraph/issues/1028
    if (!osg::isGLExtensionSupported(exts.contextID, "NV_framebuffer_multisample_coverage"))
        exts.glRenderbufferStorageMultisampleCoverageNV = nullptr;
#endif

    osg::ref_ptr<osg::Group> guiRoot = new osg::Group;
    guiRoot->setName("GUI Root");
    guiRoot->setNodeMask(MWRender::Mask_GUI);
    mStereoManager->disableStereoForNode(guiRoot);
    rootNode->addChild(guiRoot);

    mWindowManager = std::make_unique<MWGui::WindowManager>(mWindow, mViewer, guiRoot, mResourceSystem.get(),
        mWorkQueue.get(), mCfgMgr.getLogPath(), mScriptConsoleMode, mTranslationDataStorage, mEncoding, mExportFonts,
        Version::getOpenmwVersionDescription(), mCfgMgr);
    mEnvironment.setWindowManager(*mWindowManager);

    mInputManager = std::make_unique<MWInput::InputManager>(mWindow, mViewer, mScreenCaptureHandler, keybinderUser,
        keybinderUserExists, userGameControllerdb, gameControllerdb, mGrab);
    mEnvironment.setInputManager(*mInputManager);

    // Create sound system
    mSoundManager = std::make_unique<MWSound::SoundManager>(mVFS.get(), mUseSound);
    mEnvironment.setSoundManager(*mSoundManager);

    // Create the world
    mWorld = std::make_unique<MWWorld::World>(
        mResourceSystem.get(), mActivationDistanceOverride, mCellName, mCfgMgr.getUserDataPath());
    mEnvironment.setWorld(*mWorld);
    mEnvironment.setWorldModel(mWorld->getWorldModel());
    mEnvironment.setESMStore(mWorld->getStore());

    const MWWorld::Store<ESM::GameSetting>* gmst = &mWorld->getStore().get<ESM::GameSetting>();
    mL10nManager->setGmstLoader([gmst, misses = std::set<std::string, Misc::StringUtils::CiComp>()](
                                    std::string_view gmstName) mutable -> const std::string* {
        const ESM::GameSetting* res = gmst->search(gmstName);
        if (res && res->mValue.getType() == ESM::VT_String)
            return &res->mValue.getString();
        if (misses.emplace(gmstName).second)
            Log(Debug::Error) << "GMST " << gmstName << " not found";
        return nullptr;
    });

    mWindowManager->setStore(mWorld->getStore());

    // Load translation data
    mTranslationDataStorage.setEncoder(mEncoder.get());
    for (auto& mContentFile : mContentFiles)
        mTranslationDataStorage.loadTranslationData(mFileCollections, mContentFile);

    Compiler::registerExtensions(mExtensions);

    // Create script system
    mScriptContext = std::make_unique<MWScript::CompilerContext>(MWScript::CompilerContext::Type_Full);
    mScriptContext->setExtensions(&mExtensions);

    mScriptManager = std::make_unique<MWScript::ScriptManager>(mWorld->getStore(), *mScriptContext, mWarningsMode);
    mEnvironment.setScriptManager(*mScriptManager);

    // Create game mechanics system
    mMechanicsManager = std::make_unique<MWMechanics::MechanicsManager>();
    mEnvironment.setMechanicsManager(*mMechanicsManager);

    // Create dialog system
    mJournal = std::make_unique<MWDialogue::Journal>();
    mEnvironment.setJournal(*mJournal);

    mDialogueManager = std::make_unique<MWDialogue::DialogueManager>(mExtensions, mTranslationDataStorage);
    mEnvironment.setDialogueManager(*mDialogueManager);

    mLuaManager->loadPermanentStorage(mCfgMgr.getUserConfigPath());
    mLuaManager->initPreLoad();

    Loading::Listener* listener = MWBase::Environment::get().getWindowManager()->getLoadingScreen();
    Loading::AsyncListener asyncListener(*listener);
    auto dataLoading = std::async(std::launch::async,
        [&] { mWorld->loadData(mFileCollections, mContentFiles, mGroundcoverFiles, mEncoder.get(), &asyncListener); });

    if (!mSkipMenu)
    {
        std::string_view logo = Fallback::Map::getString("Movies_Company_Logo");
        if (!logo.empty())
            mWindowManager->playVideo(logo, true);
    }

    listener->loadingOn();
    {
        using namespace std::chrono_literals;
        while (dataLoading.wait_for(50ms) != std::future_status::ready)
        {
            asyncListener.update();
            mInputManager->update(0.f, true, true);
        }
        dataLoading.get();
    }
    listener->loadingOff();

    if (mStateManager->hasQuitRequest())
        return;

    mWorld->init(mMaxRecastLogLevel, mViewer, std::move(rootNode), mWorkQueue.get(), *mUnrefQueue);
    mEnvironment.setWorldScene(mWorld->getWorldScene());
    mWorld->setupPlayer();
    mWorld->setRandomSeed(mRandomSeed);
    mWindowManager->initUI();
    mLuaManager->initPostLoad();

    // scripts
    if (mCompileAll)
    {
        std::pair<int, int> result = mScriptManager->compileAll();
        if (result.first)
            Log(Debug::Info) << "compiled " << result.second << " of " << result.first << " scripts ("
                             << 100 * static_cast<double>(result.second) / result.first << "%)";
    }
    if (mCompileAllDialogue)
    {
        std::pair<int, int> result = MWDialogue::ScriptTest::compileAll(&mExtensions, mWarningsMode);
        if (result.first)
            Log(Debug::Info) << "compiled " << result.second << " of " << result.first << " dialogue scripts ("
                             << 100 * static_cast<double>(result.second) / result.first << "%)";
    }

    // starts a separate lua thread if "lua num threads" > 0
    mLuaWorker = std::make_unique<MWLua::Worker>(*mLuaManager);
}

void OMW::Engine::prepareServerSimulation()
{
    assert(!mContentFiles.empty());

    if (mServerSimulationPrepared)
        return;

    mServerSimulationMode = true;
    OMW::setServerSimulationModeActive(true);
    mServerSimulationWorldSavePath.clear();
    mServerSimulationWorldManifestPath = serverWorldManifestPath(mServerSimulationSavesPath);
    mServerSimulationWorldLoadedFromSave = false;
    mServerSimulationWorldInitializedNew = false;
    Log(Debug::Info) << "Preparing server OpenMW simulation runtime";

    Log(Debug::Info) << "OSG version: " << osgGetVersion();
    SDL_version sdlVersion;
    SDL_GetVersion(&sdlVersion);
    Log(Debug::Info) << "SDL version: " << (int)sdlVersion.major << "." << (int)sdlVersion.minor << "."
                     << (int)sdlVersion.patch;

    Misc::Rng::init(mRandomSeed);

    Settings::ShaderManager::get().load(mCfgMgr.getUserConfigPath() / "shaders.yaml");

    MWClass::registerClasses();

    mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(mEncoding);

    mViewer = new osgViewer::Viewer;
    mViewer->setReleaseContextAtEndOfFrameHint(false);
    mViewer->setUseConfigureAffinity(false);

    mEnvironment.setFrameRateLimit(0.f);

    prepareEngine();
    hideServerSimulationWindow();

    if (mStateManager->hasQuitRequest())
        return;

    std::filesystem::path serverWorldSavePath;
    bool loadedServerWorldFromSave = false;
    bool initializedNewServerWorld = false;
    const std::filesystem::path manifestPath = mServerSimulationWorldManifestPath;

    if (!mSaveGameFile.empty())
    {
        Log(Debug::Info) << "Loading explicit server OpenMW world save " << mSaveGameFile;
        serverWorldSavePath = mSaveGameFile;
        mStateManager->loadGame(mSaveGameFile);
        loadedServerWorldFromSave = mStateManager->getState() == MWState::StateManager::State_Running;
    }
    else if (!mServerSimulationSavesPath.empty())
    {
        const MWState::Character* character = nullptr;
        const MWState::Slot* slot = findMostRecentSaveSlot(*mStateManager, character);
        const ServerWorldManifest manifest = readServerWorldManifest(manifestPath,
            mServerSimulationContentPlanFingerprint, mServerSimulationWorldDatabaseFingerprint,
            mServerSimulationServerWorldCompatibilityFingerprint);
        if (manifest.exists && manifest.matches && !manifest.savePath.empty()
            && std::filesystem::is_regular_file(manifest.savePath))
        {
            Log(Debug::Info) << "Loading manifest-pinned server OpenMW world save " << manifest.savePath;
            serverWorldSavePath = manifest.savePath;
            mStateManager->loadGame(manifest.savePath);
        }
        else if (manifest.exists && manifest.matches && !manifest.savePath.empty())
            Log(Debug::Warning)
                << "Server OpenMW world manifest save is missing; initializing a fresh world: " << manifest.savePath;
        else if (!manifest.exists && slot != nullptr)
        {
            Log(Debug::Info) << "Loading legacy server OpenMW world save " << slot->mPath;
            serverWorldSavePath = slot->mPath;
            mStateManager->loadGame(character, slot->mPath);
        }
        else if (manifest.exists && !manifest.matches)
            Log(Debug::Warning)
                << "Server OpenMW world save manifest does not match current content database; initializing a fresh world";

        if (mStateManager->getState() != MWState::StateManager::State_Running)
        {
            loadedServerWorldFromSave = false;
            if (manifest.exists && manifest.matches && !manifest.savePath.empty()
                && std::filesystem::is_regular_file(manifest.savePath))
                Log(Debug::Warning) << "Server OpenMW world save did not reach running state; initializing a fresh world";
            else if (!manifest.exists && slot == nullptr)
                Log(Debug::Info) << "Initializing new server OpenMW world in " << mServerSimulationSavesPath;
            else if (manifest.exists && manifest.matches && manifest.savePath.empty())
                Log(Debug::Warning)
                    << "Server OpenMW world manifest has no save path; initializing a fresh world";

            mStateManager->newGame(true);
            initializedNewServerWorld = true;

            if (mStateManager->getState() == MWState::StateManager::State_Running)
            {
                mStateManager->saveGame("CommunityMP Server World");
                const MWState::Character* savedCharacter = nullptr;
                if (const MWState::Slot* savedSlot = findMostRecentSaveSlot(*mStateManager, savedCharacter))
                    serverWorldSavePath = savedSlot->mPath;
            }
        }
        else if (!serverWorldSavePath.empty())
            loadedServerWorldFromSave = true;
    }
    else
    {
        mStateManager->newGame(true);
        initializedNewServerWorld = true;
    }

    if (mStateManager->getState() != MWState::StateManager::State_Running)
        return;

    mServerSimulationWorldSavePath = serverWorldSavePath;
    mServerSimulationWorldLoadedFromSave = loadedServerWorldFromSave;
    mServerSimulationWorldInitializedNew = initializedNewServerWorld;
    neutralizeServerSimulationPlayer();

    if (!serverWorldSavePath.empty())
        writeServerWorldManifestIfChanged(manifestPath, mServerSimulationContentPlanFingerprint,
            mServerSimulationWorldDatabaseFingerprint, mServerSimulationServerWorldCompatibilityFingerprint,
            serverWorldSavePath);

    if (!mStartupScript.empty() && mStateManager->getState() == MWState::StateManager::State_Running)
        mWindowManager->executeInConsole(mStartupScript);

    mServerSimulationPrepared = true;
    Log(Debug::Info) << "Server OpenMW simulation runtime prepared";
}

bool OMW::Engine::tickServerSimulation(float deltaSeconds)
{
    return tickServerSimulation(deltaSeconds, deltaSeconds);
}

bool OMW::Engine::tickServerSimulation(float simulationDeltaSeconds, float clockDeltaSeconds)
{
    if (!mServerSimulationPrepared || mViewer == nullptr || mStateManager == nullptr || mWorld == nullptr
        || mStateManager->hasQuitRequest())
        return false;

    constexpr float maxSimulationInterval = 0.2f;
    const float clampedSimulationDeltaSeconds = std::clamp(simulationDeltaSeconds, 0.f, maxSimulationInterval);
    const float clampedClockDeltaSeconds = std::clamp(clockDeltaSeconds, 0.f, maxSimulationInterval);

    MWWorld::DateTimeManager& timeManager = *mWorld->getTimeManager();
    const double simulationDt = clampedSimulationDeltaSeconds * timeManager.getSimulationTimeScale();
    const double clockDt = clampedClockDeltaSeconds * timeManager.getSimulationTimeScale();

    // Headless ticks do not pass through the interactive run loop, so refresh pause state before simulation systems
    // read it.
    timeManager.updateIsPaused();

    mViewer->advance(timeManager.getRenderingSimulationTime());

    const unsigned frameNumber = mViewer->getFrameStamp()->getFrameNumber();

    neutralizeServerSimulationPlayer();
    if (!frame(frameNumber, static_cast<float>(simulationDt)))
        return false;
    neutralizeServerSimulationPlayer();

    timeManager.updateIsPaused();
    if (!timeManager.isPaused())
    {
        timeManager.setSimulationTime(timeManager.getSimulationTime() + clockDt);
        timeManager.setRenderingSimulationTime(timeManager.getRenderingSimulationTime() + clockDt);
    }

    return true;
}

void OMW::Engine::setServerSimulationPlayerActors(const std::vector<mwmp::SimulationPlayerTarget>& players)
{
    std::map<mwmp::PacketGuid, ServerSimulationPlayerActorState> previousPlayerActors
        = std::move(mServerSimulationPlayerActors);
    mServerSimulationPlayerActors.clear();
    if (!mServerSimulationMode)
    {
        for (auto& [guid, state] : previousPlayerActors)
        {
            static_cast<void>(guid);
            clearServerSimulationPlayerActorReference(state);
        }
        return;
    }

    for (const mwmp::SimulationPlayerTarget& player : players)
    {
        if (!mwmp::isPacketGuidAssigned(player.guid) || !player.hasPosition)
            continue;

        const std::string cellDescription = player.cell.getDescription();
        if (cellDescription.empty())
            continue;

        ServerSimulationPlayerActorState state;
        if (auto previous = previousPlayerActors.find(player.guid); previous != previousPlayerActors.end())
        {
            state = std::move(previous->second);
            previousPlayerActors.erase(previous);
        }

        const bool baseChanged = hasDifferentServerSimulationPlayerBase(state, player);
        if (baseChanged)
            clearServerSimulationPlayerActorReference(state);

        state.cell = player.cell;
        state.position = player.position;
        state.name = player.name;
        if (player.hasBaseInfo)
        {
            state.npc = player.npc;
            state.hasBaseInfo = true;
        }
        if (player.hasClass)
        {
            state.classId = player.classId;
            state.hasClass = true;
        }
        if (player.hasEquipmentData)
        {
            state.equipmentItems = player.equipmentItems;
            state.hasEquipmentData = true;
        }
        if (player.hasBaseStatsData)
        {
            state.baseStats = player.baseStats;
            state.hasBaseStatsData = true;
        }
        if (player.hasStatsDynamicData && hasFiniteSimpleCreatureStats(player.creatureStats))
        {
            state.stats = player.creatureStats;
            state.hasStatsDynamicData = true;
        }
        mServerSimulationPlayerActors[player.guid] = std::move(state);
    }

    for (auto& [guid, state] : previousPlayerActors)
    {
        static_cast<void>(guid);
        clearServerSimulationPlayerActorReference(state);
    }

    for (auto it = mServerSimulationActorPlayerTargets.begin(); it != mServerSimulationActorPlayerTargets.end();)
    {
        if (it->second.isPlayer && mwmp::isPacketGuidAssigned(it->second.guid)
            && mServerSimulationPlayerActors.find(it->second.guid) == mServerSimulationPlayerActors.end())
        {
            it = mServerSimulationActorPlayerTargets.erase(it);
            continue;
        }

        ++it;
    }

    syncServerSimulationPlayerActorReferences();
}

bool OMW::Engine::focusServerSimulationCell(const ESM::Cell& cell, const ESM::Position* focusPosition,
    mwmp::PacketGuid playerGuid, std::string_view playerName, const mwmp::SimpleCreatureStats* playerStats,
    const ESM::NPC* playerNpc, const ESM::RefId* playerClassId,
    const mwmp::SimulationPlayerBaseStats* playerBaseStats,
    const std::array<mwmp::Item, mwmp::equipmentSlotCount>* playerEquipmentItems)
{
    if (!mServerSimulationPrepared || mWorld == nullptr || mStateManager == nullptr || mStateManager->hasQuitRequest())
        return false;

    const std::string cellDescription = cell.getDescription();
    if (cellDescription.empty())
        return false;

    ESM::Position position = focusPosition != nullptr ? *focusPosition : ESM::Position{};
    try
    {
        const bool hasFocusPlayer = focusPosition != nullptr && mwmp::isPacketGuidAssigned(playerGuid);
        if (hasFocusPlayer)
        {
            mServerSimulationFocusPlayerGuid = playerGuid;
            mServerSimulationFocusPlayerName = std::string(playerName);
            mServerSimulationFocusPlayerSet = true;
            if (playerStats != nullptr && hasFiniteSimpleCreatureStats(*playerStats))
            {
                mServerSimulationFocusPlayerStats = *playerStats;
                mServerSimulationFocusPlayerStatsSet = true;
            }
            else
                mServerSimulationFocusPlayerStatsSet = false;
        }
        else
        {
            mServerSimulationFocusPlayerGuid = mwmp::unassignedPacketGuid();
            mServerSimulationFocusPlayerName.clear();
            mServerSimulationFocusPlayerSet = false;
            mServerSimulationFocusPlayerStatsSet = false;
        }

        if (mServerSimulationFocusCellDescription == cellDescription)
        {
            if (focusPosition != nullptr && (!mServerSimulationFocusPositionSet
                    || mServerSimulationFocusPosition != position))
            {
                MWWorld::Ptr player = mWorld->getPlayerPtr();
                mWorld->moveObject(player, position.asVec3(), true);
                mWorld->rotateObject(player, position.asRotationVec3());
                mServerSimulationFocusPosition = position;
                mServerSimulationFocusPositionSet = true;
            }
            applyServerSimulationFocusPlayerIdentity(playerName, playerNpc, playerClassId);
            if (playerBaseStats != nullptr)
                applyServerSimulationActorBaseStats(mWorld->getPlayerPtr(), *playerBaseStats, playerName);
            applyServerSimulationFocusPlayerStats();
            if (playerEquipmentItems != nullptr)
                applyServerSimulationEquipmentToActor(mWorld->getPlayerPtr(), *playerEquipmentItems, playerName);
            syncServerSimulationPlayerActorReferences();
            return true;
        }

        if (cell.isExterior())
        {
            if (focusPosition == nullptr)
            {
                const ESM::ExteriorCellLocation cellLocation(
                    cell.mData.mX, cell.mData.mY, ESM::Cell::sDefaultWorldspaceId);
                const osg::Vec2f cellCenter = ESM::indexToPosition(cellLocation, true);
                position.pos[0] = cellCenter.x();
                position.pos[1] = cellCenter.y();
                position.pos[2] = 0.f;
            }
            mWorld->changeToCell(
                ESM::RefId::esm3ExteriorCell(cell.mData.mX, cell.mData.mY), position, focusPosition == nullptr, false);
        }
        else
            mWorld->changeToInteriorCell(cell.mName, position, focusPosition == nullptr, false);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "Failed to focus server OpenMW simulation cell " << cellDescription << ": " << e.what();
        return false;
    }

    mServerSimulationFocusCellDescription = cellDescription;
    if (focusPosition != nullptr)
    {
        mServerSimulationFocusPosition = position;
        mServerSimulationFocusPositionSet = true;
    }
    else
        mServerSimulationFocusPositionSet = false;

    neutralizeServerSimulationPlayer();
    applyServerSimulationFocusPlayerIdentity(playerName, playerNpc, playerClassId);
    if (playerBaseStats != nullptr)
        applyServerSimulationActorBaseStats(mWorld->getPlayerPtr(), *playerBaseStats, playerName);
    applyServerSimulationFocusPlayerStats();
    if (playerEquipmentItems != nullptr)
        applyServerSimulationEquipmentToActor(mWorld->getPlayerPtr(), *playerEquipmentItems, playerName);
    syncServerSimulationPlayerActorReferences();

    Log(Debug::Info) << "Focused server OpenMW simulation cell " << cellDescription
                     << (focusPosition != nullptr ? " at player position" : " without player position");
    return true;
}

bool OMW::Engine::startServerSimulationActorCombatWithPlayer(const ESM::Cell& cell, std::string_view actorRefId,
    unsigned int actorRefNum, unsigned int actorMpNum, const ESM::Position& playerPosition,
    mwmp::PacketGuid playerGuid, std::string_view playerName, const mwmp::SimpleCreatureStats* playerStats,
    const ESM::NPC* playerNpc, const ESM::RefId* playerClassId,
    const mwmp::SimulationPlayerBaseStats* playerBaseStats,
    const std::array<mwmp::Item, mwmp::equipmentSlotCount>* playerEquipmentItems)
{
    if (!mServerSimulationPrepared || mWorld == nullptr || mMechanicsManager == nullptr
        || !mwmp::isPacketGuidAssigned(playerGuid))
        return false;

    if (!focusServerSimulationCell(
            cell, &playerPosition, playerGuid, playerName, playerStats, playerNpc, playerClassId,
            playerBaseStats, playerEquipmentItems))
        return false;

    MWWorld::CellStore* cellStore = mWorld->getPlayerPtr().getCell();
    MWWorld::Ptr actor = findServerSimulationActor(cellStore, actorRefId, actorRefNum, actorMpNum);
    if (actor.isEmpty())
        return false;

    mwmp::Target target;
    target.isPlayer = true;
    target.guid = playerGuid;
    target.name = std::string(playerName);
    const ESM::RefNum resolvedRefNum = actor.getCellRef().getRefNum();
    const std::string actorKey = makeServerSimulationActorIdentityKey(
        cell, actor.getCellRef().getRefId().serializeText(), resolvedRefNum.mIndex, actorMpNum);
    if (!actorKey.empty())
        mServerSimulationActorPlayerTargets[actorKey] = std::move(target);

    MWWorld::Ptr player = mWorld->getPlayerPtr();
    actor.getClass().getCreatureStats(actor).setAttacked(true);
    mMechanicsManager->startCombat(actor, player, nullptr);
    return true;
}

void OMW::Engine::exportServerSimulationActorSnapshots(std::vector<mwmp::BaseActorList>& actorLists)
{
    if (!mServerSimulationPrepared || mWorld == nullptr)
        return;

    const MWWorld::Ptr player = mWorld->getPlayerPtr();
    const mwmp::PacketGuid focusPlayerGuid = mServerSimulationFocusPlayerSet
        ? mServerSimulationFocusPlayerGuid
        : mwmp::unassignedPacketGuid();
    const std::string_view focusPlayerName = mServerSimulationFocusPlayerSet
        ? std::string_view(mServerSimulationFocusPlayerName)
        : std::string_view();
    for (MWWorld::CellStore* cellStore : mWorld->getWorldScene().getActiveCells())
    {
        if (cellStore == nullptr || cellStore->getCell() == nullptr)
            continue;

        mwmp::BaseActorList actorList;
        actorList.guid = mwmp::unassignedPacketGuid();
        actorList.cell = mwmp::makeActorPacketCell(*cellStore->getCell());
        actorList.action = mwmp::BaseActorList::SET;
        actorList.isValid = true;
        actorList.count = 0;

        cellStore->forEachType<ESM::NPC>([&](const MWWorld::Ptr& ptr) {
            if (ptr == player || isServerSimulationPlayerActorReference(ptr))
                return true;

            return appendServerSimulationActor(actorList, ptr, player, mServerSimulationActorPlayerTargets,
                mServerSimulationPlayerActors, focusPlayerGuid, focusPlayerName);
        });
        cellStore->forEachType<ESM::Creature>([&](const MWWorld::Ptr& ptr) {
            if (ptr == player || isServerSimulationPlayerActorReference(ptr))
                return true;

            return appendServerSimulationActor(actorList, ptr, player, mServerSimulationActorPlayerTargets,
                mServerSimulationPlayerActors, focusPlayerGuid, focusPlayerName);
        });

        actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
        if (actorList.count != 0)
            actorLists.push_back(std::move(actorList));
    }
}

void OMW::Engine::exportServerSimulationPlayerActorSnapshots(
    std::vector<mwmp::SimulationPlayerSnapshot>& playerSnapshots) const
{
    if (!mServerSimulationPrepared || !mServerSimulationMode)
        return;

    for (const auto& [guid, state] : mServerSimulationPlayerActors)
    {
        if (!mwmp::isPacketGuidAssigned(guid) || state.cell.getDescription().empty())
            continue;

        mwmp::SimulationPlayerSnapshot snapshot;
        snapshot.cell = state.cell;
        snapshot.position = state.position;
        snapshot.guid = guid;
        snapshot.name = state.name;
        snapshot.hasPositionData = true;

        if (!state.ptr.isEmpty() && state.ptr.getClass().isActor() && state.ptr.getCell() != nullptr
            && state.ptr.getCell()->getCell() != nullptr)
        {
            snapshot.cell = mwmp::makeActorPacketCell(*state.ptr.getCell()->getCell());
            snapshot.position = state.ptr.getRefData().getPosition();

            const MWMechanics::CreatureStats& creatureStats = state.ptr.getClass().getCreatureStats(state.ptr);
            snapshot.creatureStats.mDead = creatureStats.isDead();
            snapshot.creatureStats.mDeathAnimationFinished = creatureStats.isDeathAnimationFinished();
            for (int i = 0; i < 3; ++i)
                copyDynamicStat(creatureStats.getDynamic(i), snapshot.creatureStats.mDynamic[i]);
            snapshot.hasStatsDynamicData = true;
        }
        else if (state.hasStatsDynamicData)
        {
            snapshot.creatureStats = state.stats;
            snapshot.hasStatsDynamicData = true;
        }
        playerSnapshots.push_back(std::move(snapshot));
    }
}

bool OMW::Engine::exportServerSimulationFocusPlayerSnapshot(mwmp::SimulationPlayerSnapshot& snapshot) const
{
    if (!mServerSimulationPrepared || mWorld == nullptr || !mServerSimulationFocusPlayerSet
        || !mwmp::isPacketGuidAssigned(mServerSimulationFocusPlayerGuid))
        return false;

    const MWWorld::Ptr player = mWorld->getPlayerPtr();
    if (player.isEmpty() || !player.getClass().isActor() || player.getCell() == nullptr
        || player.getCell()->getCell() == nullptr)
        return false;

    snapshot = mwmp::SimulationPlayerSnapshot{};
    snapshot.cell = mwmp::makeActorPacketCell(*player.getCell()->getCell());
    snapshot.position = player.getRefData().getPosition();
    snapshot.guid = mServerSimulationFocusPlayerGuid;
    snapshot.name = mServerSimulationFocusPlayerName;
    snapshot.hasPositionData = true;

    const MWMechanics::CreatureStats& creatureStats = player.getClass().getCreatureStats(player);
    snapshot.creatureStats.mDead = creatureStats.isDead();
    snapshot.creatureStats.mDeathAnimationFinished = creatureStats.isDeathAnimationFinished();
    for (int i = 0; i < 3; ++i)
        copyDynamicStat(creatureStats.getDynamic(i), snapshot.creatureStats.mDynamic[i]);
    snapshot.hasStatsDynamicData = true;

    return true;
}

// Initialise and enter main loop.
void OMW::Engine::go()
{
    assert(!mContentFiles.empty());

    Log(Debug::Info) << "OSG version: " << osgGetVersion();
    SDL_version sdlVersion;
    SDL_GetVersion(&sdlVersion);
    Log(Debug::Info) << "SDL version: " << (int)sdlVersion.major << "." << (int)sdlVersion.minor << "."
                     << (int)sdlVersion.patch;

    Misc::Rng::init(mRandomSeed);

    Settings::ShaderManager::get().load(mCfgMgr.getUserConfigPath() / "shaders.yaml");

    MWClass::registerClasses();

    // Create encoder
    mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(mEncoding);

    // Setup viewer
    mViewer = new osgViewer::Viewer;
    mViewer->setReleaseContextAtEndOfFrameHint(false);

    // Do not try to outsmart the OS thread scheduler (see bug #4785).
    mViewer->setUseConfigureAffinity(false);

    mEnvironment.setFrameRateLimit(Settings::video().mFramerateLimit);

    prepareEngine();

    if (mStateManager->hasQuitRequest())
        return;

#ifdef _WIN32
    const auto* statsFile = _wgetenv(L"OPENMW_OSG_STATS_FILE");
#else
    const auto* statsFile = std::getenv("OPENMW_OSG_STATS_FILE");
#endif

    std::filesystem::path path;
    if (statsFile != nullptr)
        path = statsFile;

    std::ofstream stats;
    if (!path.empty())
    {
        stats.open(path, std::ios_base::out);
        if (stats.is_open())
            Log(Debug::Info) << "OSG stats will be written to: " << path;
        else
            Log(Debug::Warning) << "Failed to open file to write OSG stats \"" << path
                                << "\": " << std::generic_category().message(errno);
    }

    // Setup profiler
    osg::ref_ptr<Resource::Profiler> statsHandler = new Resource::Profiler(stats.is_open(), *mVFS);

    initStatsHandler(*statsHandler);

    mViewer->addEventHandler(statsHandler);

    osg::ref_ptr<Resource::StatsHandler> resourcesHandler = new Resource::StatsHandler(stats.is_open(), *mVFS);
    mViewer->addEventHandler(resourcesHandler);

    if (stats.is_open())
        Resource::collectStatistics(*mViewer);

    bool skipDefaultGameStart = false;

#ifdef BUILD_TES3MP_CLIENT
    if (!mwmp::Main::init(mContentFiles, mFileCollections))
        return;

    if (mwmp::Main::isInitialized())
    {
        mwmp::Main::postInit();
        skipDefaultGameStart = true;
    }

    mSkipMenu = true;
#endif

    // Start the game
    if (!skipDefaultGameStart)
    {
        if (!mSaveGameFile.empty())
        {
            mStateManager->loadGame(mSaveGameFile);
        }
        else if (!mSkipMenu)
        {
            // start in main menu
            mWindowManager->pushGuiMode(MWGui::GM_MainMenu);

            if (mVFS->exists(MWSound::titleMusic))
                mSoundManager->streamMusic(MWSound::titleMusic, MWSound::MusicType::Normal);
            else
                Log(Debug::Warning) << "Title music not found";

            std::string_view logo = Fallback::Map::getString("Movies_Morrowind_Logo");
            if (!logo.empty())
                mWindowManager->playVideo(logo, /*allowSkipping*/ true, /*overrideSounds*/ false);
        }
        else
        {
            mStateManager->newGame(!mNewGame);
        }
    }

    if (!mStartupScript.empty() && mStateManager->getState() == MWState::StateManager::State_Running)
    {
        mWindowManager->executeInConsole(mStartupScript);
    }

    // Start the main rendering loop
    MWWorld::DateTimeManager& timeManager = *mWorld->getTimeManager();
    const auto getFrameRateLimit = [this] {
        const float frameRateLimit = mEnvironment.getFrameRateLimit();
        const float focusLossFrameRateLimit = Settings::video().mFramerateLimitOnFocusLoss;
        if (focusLossFrameRateLimit > 0.0f && !mWindowManager->isWindowFocused()
            && (frameRateLimit <= 0.0f || focusLossFrameRateLimit < frameRateLimit))
            return focusLossFrameRateLimit;
        return frameRateLimit;
    };
    float frameRateLimit = getFrameRateLimit();
    Misc::FrameRateLimiter frameRateLimiter = Misc::makeFrameRateLimiter(frameRateLimit);
    const std::chrono::steady_clock::duration maxSimulationInterval(std::chrono::milliseconds(200));
    while (!mViewer->done() && !mStateManager->hasQuitRequest())
    {
        const float effectiveFrameRateLimit = getFrameRateLimit();
        if (effectiveFrameRateLimit != frameRateLimit)
        {
            frameRateLimit = effectiveFrameRateLimit;
            frameRateLimiter = Misc::makeFrameRateLimiter(frameRateLimit);
        }

        const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(
                              std::min(frameRateLimiter.getLastFrameDuration(), maxSimulationInterval))
                              .count()
            * timeManager.getSimulationTimeScale();

        mViewer->advance(timeManager.getRenderingSimulationTime());

        const unsigned frameNumber = mViewer->getFrameStamp()->getFrameNumber();

        if (!frame(frameNumber, static_cast<float>(dt)))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        timeManager.updateIsPaused();
        bool advanceSimulationTime = !timeManager.isPaused();
#ifdef BUILD_TES3MP_CLIENT
        if (mwmp::Main::shouldRunWorldWhilePaused())
            advanceSimulationTime = true;
#endif
        if (advanceSimulationTime)
        {
            timeManager.setSimulationTime(timeManager.getSimulationTime() + dt);
            timeManager.setRenderingSimulationTime(timeManager.getRenderingSimulationTime() + dt);
        }

        if (stats)
        {
            // The delay is required because rendering happens in parallel to the main thread and stats from there is
            // available with delay.
            constexpr unsigned statsReportDelay = 3;
            if (frameNumber >= statsReportDelay)
            {
                // Viewer frame number can be different from frameNumber because of loading screens which render new
                // frames inside a simulation frame.
                const unsigned currentFrameNumber = mViewer->getFrameStamp()->getFrameNumber();
                for (unsigned i = frameNumber; i <= currentFrameNumber; ++i)
                    reportStats(i - statsReportDelay, *mViewer, stats);
            }
        }

        frameRateLimiter.limit();
    }

    mLuaWorker->join();

#ifdef BUILD_TES3MP_CLIENT
    mwmp::ClientSettings::removeUserSettingsFromRuntimeStore(mCfgMgr);
#endif

    // Save user settings
    Settings::Manager::saveUser(mCfgMgr.getUserConfigPath() / "settings.cfg");
    Settings::ShaderManager::get().save();
    mLuaManager->savePermanentStorage(mCfgMgr.getUserConfigPath());
}

void OMW::Engine::setCompileAll(bool all)
{
    mCompileAll = all;
}

void OMW::Engine::setCompileAllDialogue(bool all)
{
    mCompileAllDialogue = all;
}

void OMW::Engine::setSoundUsage(bool soundUsage)
{
    mUseSound = soundUsage;
}

void OMW::Engine::setEncoding(const ToUTF8::FromType& encoding)
{
    mEncoding = encoding;
}

void OMW::Engine::setScriptConsoleMode(bool enabled)
{
    mScriptConsoleMode = enabled;
}

void OMW::Engine::setStartupScript(const std::filesystem::path& path)
{
    mStartupScript = path;
}

void OMW::Engine::setActivationDistanceOverride(int distance)
{
    mActivationDistanceOverride = distance;
}

void OMW::Engine::setWarningsMode(int mode)
{
    mWarningsMode = mode;
}

void OMW::Engine::enableFontExport(bool exportFonts)
{
    mExportFonts = exportFonts;
}

void OMW::Engine::setSaveGameFile(const std::filesystem::path& savegame)
{
    mSaveGameFile = savegame;
}

void OMW::Engine::setServerSimulationSavesPath(const std::filesystem::path& path)
{
    mServerSimulationSavesPath = path;
}

void OMW::Engine::setServerSimulationContentFingerprints(
    std::string contentPlanFingerprint, std::string worldDatabaseFingerprint,
    std::string serverWorldCompatibilityFingerprint)
{
    mServerSimulationContentPlanFingerprint = std::move(contentPlanFingerprint);
    mServerSimulationWorldDatabaseFingerprint = std::move(worldDatabaseFingerprint);
    mServerSimulationServerWorldCompatibilityFingerprint = std::move(serverWorldCompatibilityFingerprint);
}

void OMW::Engine::setRandomSeed(unsigned int seed)
{
    mRandomSeed = seed;
}
