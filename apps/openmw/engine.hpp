#ifndef ENGINE_H
#define ENGINE_H

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <components/compiler/extensions.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/files/collections.hpp>
#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <components/settings/settings.hpp>
#include <components/translation/translation.hpp>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include "mwbase/environment.hpp"
#include "mwworld/manualref.hpp"
#include "mwworld/ptr.hpp"

namespace Resource
{
    class ResourceSystem;
}

namespace SceneUtil
{
    class WorkQueue;
    class AsyncScreenCaptureOperation;
    class UnrefQueue;
}

namespace VFS
{
    class Manager;
}

namespace Compiler
{
    class Context;
}

namespace MWLua
{
    class LuaManager;
    class Worker;
}

namespace Stereo
{
    class Manager;
}

namespace Files
{
    struct ConfigurationManager;
}

namespace ESM
{
    struct Cell;
}

namespace osgViewer
{
    class ScreenCaptureHandler;
}

namespace SceneUtil
{
    class SelectDepthFormatOperation;

    namespace Color
    {
        class SelectColorFormatOperation;
    }
}

namespace MWState
{
    class StateManager;
}

namespace MWGui
{
    class WindowManager;
}

namespace MWInput
{
    class InputManager;
}

namespace MWSound
{
    class SoundManager;
}

namespace MWWorld
{
    class World;
}

namespace MWScript
{
    class ScriptManager;
}

namespace MWMechanics
{
    class MechanicsManager;
}

namespace mwmp
{
    class BaseActorList;
    struct SimulationPlayerTarget;
    struct SimulationPlayerSnapshot;
}

namespace MWDialogue
{
    class DialogueManager;
}

namespace MWDialogue
{
    class Journal;
}

namespace L10n
{
    class Manager;
}

struct SDL_Window;

namespace OMW
{
    struct ServerSimulationPlayerActorState
    {
        ESM::Cell cell;
        ESM::Position position;
        std::string name;
        ESM::NPC npc;
        ESM::RefId classId;
        mwmp::SimulationPlayerBaseStats baseStats;
        mwmp::SimpleCreatureStats stats;
        std::array<mwmp::Item, mwmp::equipmentSlotCount> equipmentItems = {};
        std::unique_ptr<MWWorld::ManualRef> reference;
        MWWorld::Ptr ptr;
        ESM::RefId recordId;
        bool hasBaseInfo = false;
        bool hasClass = false;
        bool hasBaseStatsData = false;
        bool hasEquipmentData = false;
        bool hasStatsDynamicData = false;
    };

    /// \brief Main engine class, that brings together all the components of OpenMW
    class Engine
    {
        SDL_Window* mWindow;
        std::unique_ptr<VFS::Manager> mVFS;
        std::unique_ptr<Resource::ResourceSystem> mResourceSystem;
        osg::ref_ptr<SceneUtil::WorkQueue> mWorkQueue;
        std::unique_ptr<SceneUtil::UnrefQueue> mUnrefQueue;
        std::unique_ptr<MWWorld::World> mWorld;
        std::unique_ptr<MWSound::SoundManager> mSoundManager;
        std::unique_ptr<MWScript::ScriptManager> mScriptManager;
        std::unique_ptr<MWGui::WindowManager> mWindowManager;
        std::unique_ptr<MWMechanics::MechanicsManager> mMechanicsManager;
        std::unique_ptr<MWDialogue::DialogueManager> mDialogueManager;
        std::unique_ptr<MWDialogue::Journal> mJournal;
        std::unique_ptr<MWInput::InputManager> mInputManager;
        std::unique_ptr<MWState::StateManager> mStateManager;
        std::unique_ptr<MWLua::LuaManager> mLuaManager;
        std::unique_ptr<MWLua::Worker> mLuaWorker;
        std::unique_ptr<L10n::Manager> mL10nManager;
        MWBase::Environment mEnvironment;
        ToUTF8::FromType mEncoding;
        std::unique_ptr<ToUTF8::Utf8Encoder> mEncoder;
        Files::PathContainer mDataDirs;
        std::vector<std::string> mArchives;
        std::filesystem::path mResDir;
        osg::ref_ptr<osgViewer::Viewer> mViewer;
        osg::ref_ptr<osgViewer::ScreenCaptureHandler> mScreenCaptureHandler;
        osg::ref_ptr<SceneUtil::AsyncScreenCaptureOperation> mScreenCaptureOperation;
        osg::ref_ptr<SceneUtil::SelectDepthFormatOperation> mSelectDepthFormatOperation;
        osg::ref_ptr<SceneUtil::Color::SelectColorFormatOperation> mSelectColorFormatOperation;
        std::string mCellName;
        std::vector<std::string> mContentFiles;
        std::vector<std::string> mGroundcoverFiles;

        std::unique_ptr<Stereo::Manager> mStereoManager;

        bool mSkipMenu;
        bool mUseSound;
        bool mCompileAll;
        bool mCompileAllDialogue;
        int mWarningsMode;
        std::string mFocusName;
        bool mScriptConsoleMode;
        std::filesystem::path mStartupScript;
        int mActivationDistanceOverride;
        std::filesystem::path mSaveGameFile;
        std::filesystem::path mServerSimulationSavesPath;
        std::string mServerSimulationContentPlanFingerprint;
        std::string mServerSimulationWorldDatabaseFingerprint;
        std::string mServerSimulationServerWorldCompatibilityFingerprint;
        std::filesystem::path mServerSimulationWorldSavePath;
        std::filesystem::path mServerSimulationWorldManifestPath;
        // Grab mouse?
        bool mGrab;

        bool mExportFonts;
        unsigned int mRandomSeed;
        Debug::Level mMaxRecastLogLevel = Debug::Error;

        Compiler::Extensions mExtensions;
        std::unique_ptr<Compiler::Context> mScriptContext;

        Files::Collections mFileCollections;
        Translation::Storage mTranslationDataStorage;
        bool mNewGame;

        Files::ConfigurationManager& mCfgMgr;
        int mGlMaxTextureImageUnits;
        bool mServerSimulationMode = false;
        bool mServerSimulationPrepared = false;
        bool mServerSimulationWorldLoadedFromSave = false;
        bool mServerSimulationWorldInitializedNew = false;
        std::string mServerSimulationFocusCellDescription;
        ESM::Position mServerSimulationFocusPosition;
        bool mServerSimulationFocusPositionSet = false;
        mwmp::PacketGuid mServerSimulationFocusPlayerGuid = mwmp::unassignedPacketGuid();
        std::string mServerSimulationFocusPlayerName;
        mwmp::SimpleCreatureStats mServerSimulationFocusPlayerStats;
        bool mServerSimulationFocusPlayerSet = false;
        bool mServerSimulationFocusPlayerStatsSet = false;
        std::map<std::string, mwmp::Target> mServerSimulationActorPlayerTargets;
        std::map<mwmp::PacketGuid, ServerSimulationPlayerActorState> mServerSimulationPlayerActors;

        // not implemented
        Engine(const Engine&);
        Engine& operator=(const Engine&);

        void executeLocalScripts();

        bool frame(unsigned frameNumber, float dt);

        /// Prepare engine for game play
        void prepareEngine();

        void neutralizeServerSimulationPlayer();
        void applyServerSimulationFocusPlayerIdentity(
            std::string_view playerName, const ESM::NPC* playerNpc, const ESM::RefId* playerClassId);
        void applyServerSimulationActorBaseStats(const MWWorld::Ptr& actor,
            const mwmp::SimulationPlayerBaseStats& baseStats, std::string_view actorName);
        void applyServerSimulationFocusPlayerStats();
        ESM::RefId ensureServerSimulationPlayerActorRecord(
            mwmp::PacketGuid guid, ServerSimulationPlayerActorState& state, const MWWorld::Ptr& proxyPlayer);
        void applyServerSimulationPlayerActorStats(ServerSimulationPlayerActorState& state);
        void applyServerSimulationEquipmentToActor(const MWWorld::Ptr& actor,
            const std::array<mwmp::Item, mwmp::equipmentSlotCount>& equipmentItems, std::string_view actorName);
        void applyServerSimulationPlayerActorEquipment(ServerSimulationPlayerActorState& state);
        void clearServerSimulationPlayerActorReference(ServerSimulationPlayerActorState& state);
        void clearServerSimulationPlayerActorReferences();
        bool isServerSimulationPlayerActorReference(const MWWorld::Ptr& ptr) const;
        void syncServerSimulationPlayerActorReferences();
        void hideServerSimulationWindow();

        void createWindow();
        void setWindowIcon();

    public:
        Engine(Files::ConfigurationManager& configurationManager);
        virtual ~Engine();

        /// Set data dirs
        void setDataDirs(const Files::PathContainer& dataDirs);

        /// Add BSA archive
        void addArchive(const std::string& archive);

        /// Set resource dir
        void setResourceDir(const std::filesystem::path& parResDir);

        /// Set start cell name
        void setCell(const std::string& cellName);

        /**
         * @brief addContentFile - Adds content file (ie. esm/esp, or omwgame/omwaddon) to the content files container.
         * @param file - filename (extension is required)
         */
        void addContentFile(const std::string& file);
        void addGroundcoverFile(const std::string& file);

        /// Disable or enable all sounds
        void setSoundUsage(bool soundUsage);

        /// Skip main menu and go directly into the game
        ///
        /// \param newGame Start a new game instead off dumping the player into the game
        /// (ignored if !skipMenu).
        void setSkipMenu(bool skipMenu, bool newGame);

        void setGrabMouse(bool grab) { mGrab = grab; }

        /// Initialise and enter main loop.
        void go();

        /// Initialise the OpenMW game systems without entering the client render loop.
        void prepareServerSimulation();

        /// Advance one server-owned OpenMW simulation frame.
        bool tickServerSimulation(float deltaSeconds);
        bool tickServerSimulation(float simulationDeltaSeconds, float clockDeltaSeconds);
        void setServerSimulationPlayerActors(const std::vector<mwmp::SimulationPlayerTarget>& players);

        /// Move the server-owned OpenMW simulation scene to the requested cell.
        bool focusServerSimulationCell(const ESM::Cell& cell, const ESM::Position* focusPosition = nullptr,
            mwmp::PacketGuid playerGuid = mwmp::unassignedPacketGuid(), std::string_view playerName = {},
            const mwmp::SimpleCreatureStats* playerStats = nullptr, const ESM::NPC* playerNpc = nullptr,
            const ESM::RefId* playerClassId = nullptr,
            const mwmp::SimulationPlayerBaseStats* playerBaseStats = nullptr,
            const std::array<mwmp::Item, mwmp::equipmentSlotCount>* playerEquipmentItems = nullptr);

        /// Start native OpenMW combat against the server focus player proxy.
        bool startServerSimulationActorCombatWithPlayer(const ESM::Cell& cell, std::string_view actorRefId,
            unsigned int actorRefNum, unsigned int actorMpNum, const ESM::Position& playerPosition,
            mwmp::PacketGuid playerGuid, std::string_view playerName,
            const mwmp::SimpleCreatureStats* playerStats = nullptr, const ESM::NPC* playerNpc = nullptr,
            const ESM::RefId* playerClassId = nullptr,
            const mwmp::SimulationPlayerBaseStats* playerBaseStats = nullptr,
            const std::array<mwmp::Item, mwmp::equipmentSlotCount>* playerEquipmentItems = nullptr);
        bool applyServerSimulationPlayerMeleeAttackToActor(const ESM::Cell& cell, std::string_view actorRefId,
            unsigned int actorRefNum, unsigned int actorMpNum, const ESM::Position& playerPosition,
            mwmp::PacketGuid playerGuid, std::string_view playerName, const mwmp::Attack& attack,
            float attackStrength, const mwmp::SimpleCreatureStats* playerStats = nullptr,
            const ESM::NPC* playerNpc = nullptr, const ESM::RefId* playerClassId = nullptr,
            const mwmp::SimulationPlayerBaseStats* playerBaseStats = nullptr,
            const std::array<mwmp::Item, mwmp::equipmentSlotCount>* playerEquipmentItems = nullptr);

        /// Export actor snapshots from the server-owned OpenMW scene.
        void exportServerSimulationActorSnapshots(std::vector<mwmp::BaseActorList>& actorLists);
        void exportServerSimulationPlayerActorSnapshots(
            std::vector<mwmp::SimulationPlayerSnapshot>& playerSnapshots) const;
        bool exportServerSimulationFocusPlayerSnapshot(mwmp::SimulationPlayerSnapshot& snapshot) const;

        bool isServerSimulationPrepared() const { return mServerSimulationPrepared; }
        bool wasServerSimulationWorldLoadedFromSave() const { return mServerSimulationWorldLoadedFromSave; }
        bool wasServerSimulationWorldInitializedNew() const { return mServerSimulationWorldInitializedNew; }
        const std::filesystem::path& getServerSimulationWorldSavePath() const { return mServerSimulationWorldSavePath; }
        const std::filesystem::path& getServerSimulationWorldManifestPath() const
        {
            return mServerSimulationWorldManifestPath;
        }

        /// Compile all scripts (excludign dialogue scripts) at startup?
        void setCompileAll(bool all);

        /// Compile all dialogue scripts at startup?
        void setCompileAllDialogue(bool all);

        /// Font encoding
        void setEncoding(const ToUTF8::FromType& encoding);

        /// Enable console-only script functionality
        void setScriptConsoleMode(bool enabled);

        /// Set path for a script that is run on startup in the console.
        void setStartupScript(const std::filesystem::path& path);

        /// Override the game setting specified activation distance.
        void setActivationDistanceOverride(int distance);

        void setWarningsMode(int mode);

        void enableFontExport(bool exportFonts);

        /// Set the save game file to load after initialising the engine.
        void setSaveGameFile(const std::filesystem::path& savegame);

        /// Set the dedicated save root used by the server-owned OpenMW world simulation.
        void setServerSimulationSavesPath(const std::filesystem::path& path);

        /// Associate the server-owned OpenMW world save with the imported content database that produced it.
        void setServerSimulationContentFingerprints(
            std::string contentPlanFingerprint, std::string worldDatabaseFingerprint,
            std::string serverWorldCompatibilityFingerprint);

        void setRandomSeed(unsigned int seed);

        void setRecastMaxLogLevel(Debug::Level value) { mMaxRecastLogLevel = value; }
    };
}

#endif /* ENGINE_H */
