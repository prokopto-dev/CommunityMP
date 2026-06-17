#include "ServerEventDispatcher.hpp"

#include <string>
#include <string_view>

#include "Script/Script.hpp"
#include "ServerNetworking.hpp"
#include "ServerSimulation.hpp"

namespace
{
    bool dispatchRuntimeEvent(std::string_view eventName, mwmp::SimulationRuntimeEventArguments arguments = {})
    {
        mwmp::ServerNetworking* networking = mwmp::ServerNetworking::getPtr();
        if (networking == nullptr)
            return false;

        return networking->getServerSimulation().runtime().dispatchServerEvent(eventName, arguments);
    }

    void noteCellLoadedByPlayer(unsigned short playerId, const std::string& cellDescription)
    {
        mwmp::ServerNetworking* networking = mwmp::ServerNetworking::getPtr();
        if (networking != nullptr)
            networking->getServerSimulation().noteCellLoadedByPlayer(playerId, cellDescription);
    }

    void noteCellUnloadedByPlayer(unsigned short playerId, const std::string& cellDescription)
    {
        mwmp::ServerNetworking* networking = mwmp::ServerNetworking::getPtr();
        if (networking != nullptr)
            networking->getServerSimulation().noteCellUnloadedByPlayer(playerId, cellDescription);
    }

    void auditShadowCellAuthority(const std::string& cellDescription, const char* context)
    {
        mwmp::ServerNetworking* networking = mwmp::ServerNetworking::getPtr();
        if (networking != nullptr)
            networking->getServerSimulation().auditShadowCellAuthority(cellDescription, context);
    }

    std::string safeString(const char* value)
    {
        return value != nullptr ? value : "";
    }
}

namespace mwmp::ServerEvents
{
    void serverInit()
    {
        if (!dispatchRuntimeEvent("OnServerInit"))
            Script::Call<Script::CallbackIdentity("OnServerInit")>();
    }

    void serverPostInit()
    {
        if (!dispatchRuntimeEvent("OnServerPostInit"))
            Script::Call<Script::CallbackIdentity("OnServerPostInit")>();
    }

    void serverExit(bool restart)
    {
        if (!dispatchRuntimeEvent("OnServerExit", { SimulationRuntimeEventArgument::boolean(restart) }))
            Script::Call<Script::CallbackIdentity("OnServerExit")>(restart);
    }

    void serverScriptCrash(const char* error)
    {
        static thread_local bool handlingServerScriptCrash = false;
        if (handlingServerScriptCrash)
            return;

        handlingServerScriptCrash = true;
        try
        {
            const std::string errorString = safeString(error);
            if (!dispatchRuntimeEvent("OnServerScriptCrash", { SimulationRuntimeEventArgument::string(errorString) }))
                Script::Call<Script::CallbackIdentity("OnServerScriptCrash")>(errorString.c_str());
            handlingServerScriptCrash = false;
        }
        catch (...)
        {
            handlingServerScriptCrash = false;
            throw;
        }
    }

    void serverWindowInput(const char* input)
    {
        const std::string inputString = safeString(input);
        if (!dispatchRuntimeEvent("OnServerWindowInput", { SimulationRuntimeEventArgument::string(inputString) }))
            Script::Call<Script::CallbackIdentity("OnServerWindowInput")>(inputString.c_str());
    }

    void requestDataFileList()
    {
        if (!dispatchRuntimeEvent("OnRequestDataFileList"))
            Script::Call<Script::CallbackIdentity("OnRequestDataFileList")>();
    }

    void mpNumIncrement(int mpNum)
    {
        if (!dispatchRuntimeEvent("OnMpNumIncrement", { SimulationRuntimeEventArgument::integer(mpNum) }))
            Script::Call<Script::CallbackIdentity("OnMpNumIncrement")>(mpNum);
    }

    void playerConnect(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerConnect", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerConnect")>(playerId);
    }

    void playerDisconnect(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerDisconnect", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerDisconnect")>(playerId);
    }

    void playerBaseInfo(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerBaseInfo", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerBaseInfo")>(playerId);
    }

    void playerCellChange(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerCellChange", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerCellChange")>(playerId);
    }

    void playerEndCharGen(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerEndCharGen", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerEndCharGen")>(playerId);
    }

    void playerDeath(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerDeath", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerDeath")>(playerId);
    }

    void playerStatsDynamic(unsigned short playerId)
    {
        if (!dispatchRuntimeEvent("OnPlayerStatsDynamic", { SimulationRuntimeEventArgument::integer(playerId) }))
            Script::Call<Script::CallbackIdentity("OnPlayerStatsDynamic")>(playerId);
    }

    void playerSendMessage(unsigned short playerId, const char* message)
    {
        const std::string messageString = safeString(message);
        if (!dispatchRuntimeEvent("OnPlayerSendMessage",
                { SimulationRuntimeEventArgument::integer(playerId),
                    SimulationRuntimeEventArgument::string(messageString) }))
            Script::Call<Script::CallbackIdentity("OnPlayerSendMessage")>(playerId, messageString.c_str());
    }

    void guiAction(unsigned short playerId, int actionId, const char* data)
    {
        const std::string dataString = safeString(data);
        if (!dispatchRuntimeEvent("OnGUIAction",
                { SimulationRuntimeEventArgument::integer(playerId), SimulationRuntimeEventArgument::integer(actionId),
                    SimulationRuntimeEventArgument::string(dataString) }))
            Script::Call<Script::CallbackIdentity("OnGUIAction")>(playerId, actionId, dataString.c_str());
    }

    void actorCellChange(unsigned short playerId, const char* cellDescription)
    {
        if (!dispatchRuntimeEvent("OnActorCellChange",
                { SimulationRuntimeEventArgument::integer(playerId),
                    SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            Script::Call<Script::CallbackIdentity("OnActorCellChange")>(playerId, cellDescription);
    }

    void actorStatsDynamic(unsigned short playerId, const char* cellDescription)
    {
        if (!dispatchRuntimeEvent("OnActorStatsDynamic",
                { SimulationRuntimeEventArgument::integer(playerId),
                    SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            Script::Call<Script::CallbackIdentity("OnActorStatsDynamic")>(playerId, cellDescription);
    }

    void cellLoad(unsigned short playerId, const char* cellDescription)
    {
        const std::string cellDescriptionString = safeString(cellDescription);
        noteCellLoadedByPlayer(playerId, cellDescriptionString);
        if (!dispatchRuntimeEvent("OnCellLoad",
                { SimulationRuntimeEventArgument::integer(static_cast<int>(playerId)),
                    SimulationRuntimeEventArgument::string(cellDescriptionString) }))
            Script::Call<Script::CallbackIdentity("OnCellLoad")>(playerId, cellDescription);
        auditShadowCellAuthority(cellDescriptionString, "OnCellLoad");
    }

    void cellUnload(unsigned short playerId, const char* cellDescription)
    {
        const std::string cellDescriptionString = safeString(cellDescription);
        noteCellUnloadedByPlayer(playerId, cellDescriptionString);
        if (!dispatchRuntimeEvent("OnCellUnload",
                { SimulationRuntimeEventArgument::integer(static_cast<int>(playerId)),
                    SimulationRuntimeEventArgument::string(cellDescriptionString) }))
            Script::Call<Script::CallbackIdentity("OnCellUnload")>(playerId, cellDescription);
        auditShadowCellAuthority(cellDescriptionString, "OnCellUnload");
    }

    void cellDeletion(const char* cellDescription)
    {
        if (!dispatchRuntimeEvent(
                "OnCellDeletion", { SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            Script::Call<Script::CallbackIdentity("OnCellDeletion")>(cellDescription);
    }

    void playerEvent(const char* eventName, unsigned short playerId)
    {
        const std::string eventNameString = safeString(eventName);
        if (dispatchRuntimeEvent(eventNameString, { SimulationRuntimeEventArgument::integer(playerId) }))
            return;

#define FALLBACK_PLAYER_EVENT(callbackName) \
    if (eventNameString == #callbackName) \
    { \
        Script::Call<Script::CallbackIdentity(#callbackName)>(playerId); \
        return; \
    }

        FALLBACK_PLAYER_EVENT(OnClientScriptGlobal)
        FALLBACK_PLAYER_EVENT(OnPlayerAttribute)
        FALLBACK_PLAYER_EVENT(OnPlayerBook)
        FALLBACK_PLAYER_EVENT(OnPlayerBounty)
        FALLBACK_PLAYER_EVENT(OnPlayerCharClass)
        FALLBACK_PLAYER_EVENT(OnPlayerCooldowns)
        FALLBACK_PLAYER_EVENT(OnPlayerDisposition)
        FALLBACK_PLAYER_EVENT(OnPlayerEquipment)
        FALLBACK_PLAYER_EVENT(OnPlayerFaction)
        FALLBACK_PLAYER_EVENT(OnPlayerInput)
        FALLBACK_PLAYER_EVENT(OnPlayerInventory)
        FALLBACK_PLAYER_EVENT(OnPlayerItemUse)
        FALLBACK_PLAYER_EVENT(OnPlayerJournal)
        FALLBACK_PLAYER_EVENT(OnPlayerLevel)
        FALLBACK_PLAYER_EVENT(OnPlayerMiscellaneous)
        FALLBACK_PLAYER_EVENT(OnPlayerQuickKeys)
        FALLBACK_PLAYER_EVENT(OnPlayerReputation)
        FALLBACK_PLAYER_EVENT(OnPlayerRest)
        FALLBACK_PLAYER_EVENT(OnPlayerResurrect)
        FALLBACK_PLAYER_EVENT(OnPlayerShapeshift)
        FALLBACK_PLAYER_EVENT(OnPlayerSkill)
        FALLBACK_PLAYER_EVENT(OnPlayerSpellbook)
        FALLBACK_PLAYER_EVENT(OnPlayerSpellsActive)
        FALLBACK_PLAYER_EVENT(OnPlayerTopic)
        FALLBACK_PLAYER_EVENT(OnRecordDynamic)
        FALLBACK_PLAYER_EVENT(OnWorldKillCount)
        FALLBACK_PLAYER_EVENT(OnWorldMap)
        FALLBACK_PLAYER_EVENT(OnWorldWeather)

#undef FALLBACK_PLAYER_EVENT
    }

    void actorEvent(const char* eventName, unsigned short playerId, const char* cellDescription)
    {
        const std::string eventNameString = safeString(eventName);
        if (dispatchRuntimeEvent(eventNameString,
                { SimulationRuntimeEventArgument::integer(playerId),
                    SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            return;

#define FALLBACK_ACTOR_EVENT(callbackName) \
    if (eventNameString == #callbackName) \
    { \
        Script::Call<Script::CallbackIdentity(#callbackName)>(playerId, cellDescription); \
        return; \
    }

        FALLBACK_ACTOR_EVENT(OnActorAI)
        FALLBACK_ACTOR_EVENT(OnActorDeath)
        FALLBACK_ACTOR_EVENT(OnActorEquipment)
        FALLBACK_ACTOR_EVENT(OnActorList)
        FALLBACK_ACTOR_EVENT(OnActorSpellsActive)
        FALLBACK_ACTOR_EVENT(OnActorTest)

#undef FALLBACK_ACTOR_EVENT
    }

    void objectEvent(const char* eventName, unsigned short playerId, const char* cellDescription)
    {
        const std::string eventNameString = safeString(eventName);
        if (dispatchRuntimeEvent(eventNameString,
                { SimulationRuntimeEventArgument::integer(playerId),
                    SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            return;

#define FALLBACK_OBJECT_EVENT(callbackName) \
    if (eventNameString == #callbackName) \
    { \
        Script::Call<Script::CallbackIdentity(#callbackName)>(playerId, cellDescription); \
        return; \
    }

        FALLBACK_OBJECT_EVENT(OnConsoleCommand)
        FALLBACK_OBJECT_EVENT(OnContainer)
        FALLBACK_OBJECT_EVENT(OnDoorDestination)
        FALLBACK_OBJECT_EVENT(OnDoorState)
        FALLBACK_OBJECT_EVENT(OnObjectActivate)
        FALLBACK_OBJECT_EVENT(OnObjectHit)
        FALLBACK_OBJECT_EVENT(OnObjectPlace)
        FALLBACK_OBJECT_EVENT(OnObjectState)
        FALLBACK_OBJECT_EVENT(OnObjectSpawn)
        FALLBACK_OBJECT_EVENT(OnObjectDelete)
        FALLBACK_OBJECT_EVENT(OnObjectLock)
        FALLBACK_OBJECT_EVENT(OnObjectMove)
        FALLBACK_OBJECT_EVENT(OnObjectRotate)
        FALLBACK_OBJECT_EVENT(OnObjectDialogueChoice)
        FALLBACK_OBJECT_EVENT(OnObjectMiscellaneous)
        FALLBACK_OBJECT_EVENT(OnObjectRestock)
        FALLBACK_OBJECT_EVENT(OnObjectScale)
        FALLBACK_OBJECT_EVENT(OnObjectSound)
        FALLBACK_OBJECT_EVENT(OnObjectTrap)
        FALLBACK_OBJECT_EVENT(OnVideoPlay)
        FALLBACK_OBJECT_EVENT(OnClientScriptLocal)

#undef FALLBACK_OBJECT_EVENT
    }
}
