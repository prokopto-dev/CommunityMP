local core = require('openmw.core')
local markup = require('openmw.markup')
local auxUtil = require('openmw_aux.util')

local ok, communitymp = pcall(require, 'openmw.communitymp')
if not ok then
    return {}
end

local allServerEventHandlers = {}
local serverEventHandlersByName = {}
local cellAuthorityByDescription = {}

local function shallowCopy(value)
    if type(value) ~= 'table' then
        return value
    end

    local copy = {}
    for key, entry in pairs(value) do
        copy[key] = entry
    end
    return copy
end

local function addHandler(handlers, handler)
    if type(handler) ~= 'function' then
        error('CommunityMP server event handler must be a function')
    end

    handlers[#handlers + 1] = handler
end

local function addServerEventHandler(handler)
    addHandler(allServerEventHandlers, handler)
end

local function addServerEventHandlerForName(eventName, handler)
    if type(eventName) ~= 'string' or eventName == '' then
        error('CommunityMP server event name must be a non-empty string')
    end

    local handlers = serverEventHandlersByName[eventName]
    if handlers == nil then
        handlers = {}
        serverEventHandlersByName[eventName] = handlers
    end

    addHandler(handlers, handler)
end

local function decodePayload(event)
    if type(event.payload) ~= 'string' or event.payload == '' then
        event.serverEventName = event.eventName
        event.arguments = {}
        return event
    end

    local decodedOk, decoded = pcall(markup.decodeYaml, event.payload)
    if not decodedOk or type(decoded) ~= 'table' then
        event.serverEventName = event.eventName
        event.arguments = {}
        event.payloadDecodeError = tostring(decoded)
        return event
    end

    event.decodedPayload = decoded
    event.serverEventName = type(decoded.eventName) == 'string' and decoded.eventName or event.eventName
    event.arguments = type(decoded.arguments) == 'table' and decoded.arguments or {}
    return event
end

local function dispatchServerEvent(event)
    decodePayload(event)

    if event.serverEventName == 'cell_authority' and type(event.decodedPayload) == 'table' then
        local cellDescription = event.decodedPayload.cellDescription
        if type(cellDescription) == 'string' and cellDescription ~= '' then
            cellAuthorityByDescription[cellDescription] = shallowCopy(event.decodedPayload)
        end
    end

    local stop = auxUtil.callEventHandlers(serverEventHandlersByName[event.serverEventName], event)
    if not stop then
        auxUtil.callEventHandlers(allServerEventHandlers, event)
    end

    core.sendGlobalEvent('CommunityMPServerEvent', event)
end

local function dispatchServerEvents()
    for _, event in ipairs(communitymp.receiveServerEvents()) do
        dispatchServerEvent(event)
    end
end

return {
    interfaceName = 'CommunityMP',
    interface = {
        version = 1,
        addServerEventHandler = addServerEventHandler,
        addServerEventHandlerForName = addServerEventHandlerForName,
        getCellAuthority = function(cellDescription)
            return shallowCopy(cellAuthorityByDescription[cellDescription])
        end,
        isLocalCellAuthority = function(cellDescription)
            local state = cellAuthorityByDescription[cellDescription]
            return state ~= nil and state.isAuthority == true
        end,
    },
    engineHandlers = {
        onUpdate = function()
            dispatchServerEvents()
        end,
    },
}
