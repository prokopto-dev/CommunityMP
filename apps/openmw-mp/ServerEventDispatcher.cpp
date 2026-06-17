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

    void requestDataFileList()
    {
        if (!dispatchRuntimeEvent("OnRequestDataFileList"))
            Script::Call<Script::CallbackIdentity("OnRequestDataFileList")>();
    }

    void cellLoad(unsigned short playerId, const char* cellDescription)
    {
        if (!dispatchRuntimeEvent("OnCellLoad",
                { SimulationRuntimeEventArgument::integer(static_cast<int>(playerId)),
                    SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            Script::Call<Script::CallbackIdentity("OnCellLoad")>(playerId, cellDescription);
    }

    void cellUnload(unsigned short playerId, const char* cellDescription)
    {
        if (!dispatchRuntimeEvent("OnCellUnload",
                { SimulationRuntimeEventArgument::integer(static_cast<int>(playerId)),
                    SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            Script::Call<Script::CallbackIdentity("OnCellUnload")>(playerId, cellDescription);
    }

    void cellDeletion(const char* cellDescription)
    {
        if (!dispatchRuntimeEvent(
                "OnCellDeletion", { SimulationRuntimeEventArgument::string(safeString(cellDescription)) }))
            Script::Call<Script::CallbackIdentity("OnCellDeletion")>(cellDescription);
    }
}
