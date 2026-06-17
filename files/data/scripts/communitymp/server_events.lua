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
local cellAuthorityByKey = {}

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

local function cellProperty(cell, property)
    if cell == nil then
        return nil
    end

    local ok, value = pcall(function()
        return cell[property]
    end)

    if not ok then
        return nil
    end

    return value
end

local function cellKey(cell)
    if cell == nil then
        return nil
    end

    if cellProperty(cell, 'isExterior') then
        return table.concat({
            'exterior',
            tostring(cellProperty(cell, 'worldSpaceId') or ''),
            tostring(cellProperty(cell, 'gridX') or 0),
            tostring(cellProperty(cell, 'gridY') or 0),
        }, ':')
    end

    return 'interior:' .. tostring(cellProperty(cell, 'id') or cellProperty(cell, 'name') or '')
end

local function appendAlias(aliases, seen, alias)
    if type(alias) ~= 'string' or alias == '' or seen[alias] then
        return
    end

    seen[alias] = true
    aliases[#aliases + 1] = alias
end

local function authorityLookupAliases(cellOrDescription)
    local aliases = {}
    local seen = {}

    if type(cellOrDescription) == 'string' then
        appendAlias(aliases, seen, cellOrDescription)
        return aliases
    end

    if cellOrDescription == nil then
        return aliases
    end

    appendAlias(aliases, seen, cellKey(cellOrDescription))

    if cellProperty(cellOrDescription, 'isExterior') then
        local gridX = tostring(cellProperty(cellOrDescription, 'gridX') or 0)
        local gridY = tostring(cellProperty(cellOrDescription, 'gridY') or 0)
        appendAlias(aliases, seen, 'exterior::' .. gridX .. ':' .. gridY)
        appendAlias(aliases, seen, 'exterior:' .. gridX .. ',' .. gridY)
        appendAlias(aliases, seen, gridX .. ', ' .. gridY)
    else
        local id = tostring(cellProperty(cellOrDescription, 'id') or '')
        local name = tostring(cellProperty(cellOrDescription, 'name') or '')
        appendAlias(aliases, seen, id)
        appendAlias(aliases, seen, name)
        appendAlias(aliases, seen, 'interior:' .. id)
        appendAlias(aliases, seen, 'interior:' .. name)
    end

    return aliases
end

local function storeCellAuthority(state)
    if type(state) ~= 'table' then
        return
    end

    local copy = shallowCopy(state)
    local cellDescription = copy.cellDescription
    if type(cellDescription) == 'string' and cellDescription ~= '' then
        cellAuthorityByDescription[cellDescription] = copy
    end

    local cellKeyValue = copy.cellKey
    if type(cellKeyValue) == 'string' and cellKeyValue ~= '' then
        cellAuthorityByKey[cellKeyValue] = copy
    end

    local serverCellKey = copy.serverCellKey
    if type(serverCellKey) == 'string' and serverCellKey ~= '' then
        cellAuthorityByKey[serverCellKey] = copy
    end
end

local function getCellAuthority(cellOrDescription)
    for _, alias in ipairs(authorityLookupAliases(cellOrDescription)) do
        local state = cellAuthorityByDescription[alias] or cellAuthorityByKey[alias]
        if state ~= nil then
            return shallowCopy(state)
        end
    end

    return nil
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
        storeCellAuthority(event.decodedPayload)
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
        cellKey = cellKey,
        getCellAuthority = getCellAuthority,
        getCellAuthorityForCell = getCellAuthority,
        isLocalCellAuthority = function(cellOrDescription)
            local state = getCellAuthority(cellOrDescription)
            return state ~= nil and state.isAuthority == true
        end,
    },
    engineHandlers = {
        onUpdate = function()
            dispatchServerEvents()
        end,
    },
}
