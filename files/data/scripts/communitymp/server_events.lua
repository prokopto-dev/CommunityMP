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
local cellActivityByDescription = {}
local cellActivityByKey = {}
local runtimeStatus = nil
local serverEventStats = {
    received = 0,
    dispatched = 0,
    decodeErrors = 0,
    sequenceGaps = 0,
    droppedSequences = 0,
    staleSequences = 0,
    lastSequence = nil,
}

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

local function statsSnapshot()
    return shallowCopy(serverEventStats)
end

local function updateSequenceStats(event)
    serverEventStats.received = serverEventStats.received + 1

    local sequence = tonumber(event.sequence)
    if sequence == nil then
        return
    end

    if serverEventStats.lastSequence ~= nil then
        if sequence > serverEventStats.lastSequence + 1 then
            local dropped = sequence - serverEventStats.lastSequence - 1
            serverEventStats.sequenceGaps = serverEventStats.sequenceGaps + 1
            serverEventStats.droppedSequences = serverEventStats.droppedSequences + dropped
            event.sequenceGap = dropped
        elseif sequence <= serverEventStats.lastSequence then
            serverEventStats.staleSequences = serverEventStats.staleSequences + 1
            event.staleSequence = true
        end
    end

    if serverEventStats.lastSequence == nil or sequence > serverEventStats.lastSequence then
        serverEventStats.lastSequence = sequence
    end
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

local function storeCellActivity(state)
    if type(state) ~= 'table' then
        return
    end

    local copy = shallowCopy(state)
    local cellDescription = copy.cellDescription
    if type(cellDescription) == 'string' and cellDescription ~= '' then
        cellActivityByDescription[cellDescription] = copy
    end

    local cellKeyValue = copy.cellKey
    if type(cellKeyValue) == 'string' and cellKeyValue ~= '' then
        cellActivityByKey[cellKeyValue] = copy
    end

    local serverCellKey = copy.serverCellKey
    if type(serverCellKey) == 'string' and serverCellKey ~= '' then
        cellActivityByKey[serverCellKey] = copy
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

local function getCellActivity(cellOrDescription)
    for _, alias in ipairs(authorityLookupAliases(cellOrDescription)) do
        local state = cellActivityByDescription[alias] or cellActivityByKey[alias]
        if state ~= nil then
            return shallowCopy(state)
        end
    end

    return nil
end

local function storeRuntimeStatus(state)
    if type(state) ~= 'table' then
        return
    end

    runtimeStatus = shallowCopy(state)
    if type(state.capabilities) == 'table' then
        runtimeStatus.capabilities = shallowCopy(state.capabilities)
    end
end

local function getRuntimeStatus()
    if runtimeStatus == nil then
        return nil
    end

    local copy = shallowCopy(runtimeStatus)
    if type(runtimeStatus.capabilities) == 'table' then
        copy.capabilities = shallowCopy(runtimeStatus.capabilities)
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
        serverEventStats.decodeErrors = serverEventStats.decodeErrors + 1
        return event
    end

    event.decodedPayload = decoded
    event.serverEventName = type(decoded.eventName) == 'string' and decoded.eventName or event.eventName
    event.arguments = type(decoded.arguments) == 'table' and decoded.arguments or {}
    return event
end

local function dispatchServerEvent(event)
    updateSequenceStats(event)
    decodePayload(event)

    if event.serverEventName == 'cell_authority' and type(event.decodedPayload) == 'table' then
        storeCellAuthority(event.decodedPayload)
    elseif event.serverEventName == 'cell_activity' and type(event.decodedPayload) == 'table' then
        storeCellActivity(event.decodedPayload)
    elseif event.serverEventName == 'runtime_status' and type(event.decodedPayload) == 'table' then
        storeRuntimeStatus(event.decodedPayload)
    end

    local stop = auxUtil.callEventHandlers(serverEventHandlersByName[event.serverEventName], event)
    if not stop then
        auxUtil.callEventHandlers(allServerEventHandlers, event)
    end

    core.sendGlobalEvent('CommunityMPServerEvent', event)
    serverEventStats.dispatched = serverEventStats.dispatched + 1
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
        getServerEventStats = statsSnapshot,
        getRuntimeStatus = getRuntimeStatus,
        getRuntimeCapabilities = function()
            local state = getRuntimeStatus()
            if state == nil or type(state.capabilities) ~= 'table' then
                return nil
            end
            return shallowCopy(state.capabilities)
        end,
        hasOpenMWWorld = function()
            local state = getRuntimeStatus()
            return state ~= nil and state.openmwWorld == true
        end,
        isServerActorAuthorityEnabled = function()
            local state = getRuntimeStatus()
            return state ~= nil and state.serverActorAuthority == true
        end,
        getCellAuthority = getCellAuthority,
        getCellAuthorityForCell = getCellAuthority,
        getCellActivity = getCellActivity,
        getCellActivityForCell = getCellActivity,
        isCellActive = function(cellOrDescription)
            local state = getCellActivity(cellOrDescription)
            return state ~= nil and tonumber(state.visitorCount or 0) > 0
        end,
        hasCellSimulationInterest = function(cellOrDescription)
            local state = getCellActivity(cellOrDescription)
            return state ~= nil and state.simulationInterest == true
        end,
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
