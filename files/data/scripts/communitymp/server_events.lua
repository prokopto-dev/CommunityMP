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
local latestMovementCorrection = nil
local latestPlayerPacketDecisionByPacket = {}
local latestQuestState = nil
local latestQuestDialogueEvaluation = nil
local latestQuestDialogueChoice = nil
local serverEventStats = {
    received = 0,
    dispatched = 0,
    decodeErrors = 0,
    sequenceGaps = 0,
    droppedSequences = 0,
    staleSequences = 0,
    lastSequence = nil,
}
local movementCorrectionStats = {
    received = 0,
    sentCorrections = 0,
    cellSpaceTransitions = 0,
    cellChangeCorrections = 0,
    nonFinitePositions = 0,
    implausibleMovements = 0,
    lastReason = nil,
}
local playerPacketDecisionStats = {
    received = 0,
    accepted = 0,
    rejected = 0,
    corrected = 0,
    loadSnapshots = 0,
    deltaChanges = 0,
    byPacket = {},
}
local questStateStats = {
    received = 0,
    loadSnapshots = 0,
    deltaChanges = 0,
    truncatedDeltas = 0,
    lastRevision = 0,
    knownQuestDefinitions = 0,
    knownQuestSteps = 0,
    unknownJournalQuests = 0,
    questDatabaseLoaded = false,
    eventJournalAvailable = false,
    eventJournalEventCount = 0,
    eventJournalWriteFailures = 0,
    bySourcePacket = {},
}
local questDialogueEvaluationStats = {
    received = 0,
    topicCount = 0,
    authoritativelyAccepted = 0,
    fullyServerExecutable = 0,
    conditionRejected = 0,
    requiresFallback = 0,
    evaluatedConditions = 0,
    unsupportedConditions = 0,
    legacyEffectCount = 0,
    normalizedEffectCount = 0,
    legacyScriptCount = 0,
    plannedJournalEffects = 0,
    plannedTopicEffects = 0,
    plannedUnsupportedEffects = 0,
    unsupportedEffectCommandCount = 0,
}
local questDialogueChoiceStats = {
    received = 0,
    responseCount = 0,
    authoritativeCandidates = 0,
    fullyExecutable = 0,
    authoritativeApplyEnabled = false,
    applied = 0,
    plannedJournalEffects = 0,
    plannedTopicEffects = 0,
    plannedUnsupportedEffects = 0,
    appliedEffects = 0,
    skippedDuplicateEffects = 0,
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

local function deepCopy(value)
    if type(value) ~= 'table' then
        return value
    end

    local copy = {}
    for key, entry in pairs(value) do
        copy[key] = deepCopy(entry)
    end
    return copy
end

local function statsSnapshot()
    return shallowCopy(serverEventStats)
end

local function copyMovementCorrection(state)
    if type(state) ~= 'table' then
        return nil
    end

    local copy = shallowCopy(state)
    if type(state.attemptedPosition) == 'table' then
        copy.attemptedPosition = shallowCopy(state.attemptedPosition)
    end
    if type(state.authoritativePosition) == 'table' then
        copy.authoritativePosition = shallowCopy(state.authoritativePosition)
    end
    return copy
end

local function movementCorrectionStatsSnapshot()
    return shallowCopy(movementCorrectionStats)
end

local function copyPlayerPacketDecisionStats()
    local copy = shallowCopy(playerPacketDecisionStats)
    copy.byPacket = {}
    for packetName, stats in pairs(playerPacketDecisionStats.byPacket) do
        copy.byPacket[packetName] = shallowCopy(stats)
    end
    return copy
end

local function copyQuestStateStats()
    local copy = shallowCopy(questStateStats)
    copy.bySourcePacket = {}
    for sourcePacket, stats in pairs(questStateStats.bySourcePacket) do
        copy.bySourcePacket[sourcePacket] = shallowCopy(stats)
    end
    return copy
end

local function copyQuestDialogueEvaluationStats()
    return shallowCopy(questDialogueEvaluationStats)
end

local function copyQuestDialogueChoiceStats()
    return shallowCopy(questDialogueChoiceStats)
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
    if type(copy.snapshotReporterGuid) ~= 'string' or copy.snapshotReporterGuid == '' then
        copy.snapshotReporterGuid = copy.authorityGuid
    end
    if type(copy.snapshotReporterName) ~= 'string' or copy.snapshotReporterName == '' then
        copy.snapshotReporterName = copy.authorityName
    end
    if type(copy.authorityMode) ~= 'string' or copy.authorityMode == '' then
        if copy.serverActorAuthority == true then
            copy.authorityMode = 'server-simulation'
        elseif type(copy.snapshotReporterGuid) == 'string' and copy.snapshotReporterGuid ~= '' then
            copy.authorityMode = 'client-snapshot-reporter'
        else
            copy.authorityMode = 'none'
        end
    end
    if type(copy.simulationOwner) ~= 'string' or copy.simulationOwner == '' then
        copy.simulationOwner = copy.serverActorAuthority == true and 'server' or 'client-snapshot-fallback'
    end
    if copy.serverOwnsSimulation == nil then
        copy.serverOwnsSimulation = copy.serverActorAuthority == true
    end
    if type(copy.clientRole) ~= 'string' or copy.clientRole == '' then
        if copy.serverActorAuthority == true then
            copy.clientRole = 'renderer'
        elseif copy.isAuthority == true then
            copy.clientRole = 'snapshot-reporter'
        else
            copy.clientRole = 'observer'
        end
    end

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
    if type(copy.authorityMode) ~= 'string' or copy.authorityMode == '' then
        copy.authorityMode = copy.serverActorAuthority == true and 'server-simulation' or 'client-snapshot-reporter'
    end
    if type(copy.simulationOwner) ~= 'string' or copy.simulationOwner == '' then
        copy.simulationOwner = copy.serverActorAuthority == true and 'server' or 'client-snapshot-fallback'
    end
    if copy.serverOwnsSimulation == nil then
        copy.serverOwnsSimulation = copy.serverActorAuthority == true
    end

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

local function getCellSnapshotReporter(cellOrDescription)
    local state = getCellAuthority(cellOrDescription)
    if state == nil then
        return nil
    end

    if type(state.snapshotReporterGuid) ~= 'string' or state.snapshotReporterGuid == '' then
        return nil
    end

    return state
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
    if type(state.movementPolicy) == 'table' then
        runtimeStatus.movementPolicy = shallowCopy(state.movementPolicy)
    end
    if type(state.questDatabase) == 'table' then
        runtimeStatus.questDatabase = shallowCopy(state.questDatabase)
    end
    if type(state.questEventJournal) == 'table' then
        runtimeStatus.questEventJournal = shallowCopy(state.questEventJournal)
    end
    if type(state.questRuntimeEvaluator) == 'table' then
        runtimeStatus.questRuntimeEvaluator = shallowCopy(state.questRuntimeEvaluator)
    end
end

local function storeMovementCorrection(state)
    local copy = copyMovementCorrection(state)
    if copy == nil then
        return
    end

    latestMovementCorrection = copy
    movementCorrectionStats.received = movementCorrectionStats.received + 1
    if state.sentCorrection == true then
        movementCorrectionStats.sentCorrections = movementCorrectionStats.sentCorrections + 1
    end

    local reason = type(state.reason) == 'string' and state.reason or ''
    movementCorrectionStats.lastReason = reason
    if reason == 'cell_space_transition' then
        movementCorrectionStats.cellSpaceTransitions = movementCorrectionStats.cellSpaceTransitions + 1
    elseif reason == 'cell_change_correction' then
        movementCorrectionStats.cellChangeCorrections = movementCorrectionStats.cellChangeCorrections + 1
    elseif reason == 'non_finite_position' then
        movementCorrectionStats.nonFinitePositions = movementCorrectionStats.nonFinitePositions + 1
    elseif reason == 'implausible_movement' then
        movementCorrectionStats.implausibleMovements = movementCorrectionStats.implausibleMovements + 1
    end
end

local function storePlayerPacketDecision(state)
    if type(state) ~= 'table' then
        return
    end

    local packetName = type(state.packet) == 'string' and state.packet or ''
    if packetName == '' then
        return
    end

    local copy = shallowCopy(state)
    latestPlayerPacketDecisionByPacket[packetName] = copy

    playerPacketDecisionStats.received = playerPacketDecisionStats.received + 1
    if state.accepted == true then
        playerPacketDecisionStats.accepted = playerPacketDecisionStats.accepted + 1
    else
        playerPacketDecisionStats.rejected = playerPacketDecisionStats.rejected + 1
    end
    if state.corrected == true then
        playerPacketDecisionStats.corrected = playerPacketDecisionStats.corrected + 1
    end
    if state.loadSnapshot == true then
        playerPacketDecisionStats.loadSnapshots = playerPacketDecisionStats.loadSnapshots + 1
    else
        playerPacketDecisionStats.deltaChanges = playerPacketDecisionStats.deltaChanges + 1
    end

    local packetStats = playerPacketDecisionStats.byPacket[packetName]
    if packetStats == nil then
        packetStats = {
            received = 0,
            accepted = 0,
            rejected = 0,
            corrected = 0,
            loadSnapshots = 0,
            deltaChanges = 0,
            lastReason = nil,
        }
        playerPacketDecisionStats.byPacket[packetName] = packetStats
    end

    packetStats.received = packetStats.received + 1
    if state.accepted == true then
        packetStats.accepted = packetStats.accepted + 1
    else
        packetStats.rejected = packetStats.rejected + 1
    end
    if state.corrected == true then
        packetStats.corrected = packetStats.corrected + 1
    end
    if state.loadSnapshot == true then
        packetStats.loadSnapshots = packetStats.loadSnapshots + 1
    else
        packetStats.deltaChanges = packetStats.deltaChanges + 1
    end
    packetStats.lastReason = type(state.reason) == 'string' and state.reason or ''
end

local function storeQuestState(state)
    if type(state) ~= 'table' then
        return
    end

    latestQuestState = deepCopy(state)
    questStateStats.received = questStateStats.received + 1
    questStateStats.lastRevision = tonumber(state.revision or questStateStats.lastRevision) or questStateStats.lastRevision
    questStateStats.knownQuestDefinitions = tonumber(state.knownQuestDefinitionCount or 0) or 0
    questStateStats.knownQuestSteps = tonumber(state.knownQuestStepCount or 0) or 0
    questStateStats.unknownJournalQuests = tonumber(state.unknownJournalQuestCount or 0) or 0
    questStateStats.questDatabaseLoaded = state.questDatabaseLoaded == true
    questStateStats.eventJournalAvailable = state.eventJournalAvailable == true
    questStateStats.eventJournalEventCount = tonumber(state.eventJournalEventCount or 0) or 0
    questStateStats.eventJournalWriteFailures = tonumber(state.eventJournalWriteFailures or 0) or 0
    if state.loadSnapshot == true then
        questStateStats.loadSnapshots = questStateStats.loadSnapshots + 1
    else
        questStateStats.deltaChanges = questStateStats.deltaChanges + 1
    end
    if state.deltaTruncated == true then
        questStateStats.truncatedDeltas = questStateStats.truncatedDeltas + 1
    end

    local sourcePacket = type(state.sourcePacket) == 'string' and state.sourcePacket or ''
    if sourcePacket == '' then
        return
    end

    local packetStats = questStateStats.bySourcePacket[sourcePacket]
    if packetStats == nil then
        packetStats = {
            received = 0,
            loadSnapshots = 0,
            deltaChanges = 0,
            truncatedDeltas = 0,
            lastRevision = 0,
            knownQuestDefinitions = 0,
            knownQuestSteps = 0,
            unknownJournalQuests = 0,
            questDatabaseLoaded = false,
            eventJournalAvailable = false,
            eventJournalEventCount = 0,
            eventJournalWriteFailures = 0,
        }
        questStateStats.bySourcePacket[sourcePacket] = packetStats
    end

    packetStats.received = packetStats.received + 1
    packetStats.lastRevision = tonumber(state.revision or packetStats.lastRevision) or packetStats.lastRevision
    packetStats.knownQuestDefinitions = tonumber(state.knownQuestDefinitionCount or 0) or 0
    packetStats.knownQuestSteps = tonumber(state.knownQuestStepCount or 0) or 0
    packetStats.unknownJournalQuests = tonumber(state.unknownJournalQuestCount or 0) or 0
    packetStats.questDatabaseLoaded = state.questDatabaseLoaded == true
    packetStats.eventJournalAvailable = state.eventJournalAvailable == true
    packetStats.eventJournalEventCount = tonumber(state.eventJournalEventCount or 0) or 0
    packetStats.eventJournalWriteFailures = tonumber(state.eventJournalWriteFailures or 0) or 0
    if state.loadSnapshot == true then
        packetStats.loadSnapshots = packetStats.loadSnapshots + 1
    else
        packetStats.deltaChanges = packetStats.deltaChanges + 1
    end
    if state.deltaTruncated == true then
        packetStats.truncatedDeltas = packetStats.truncatedDeltas + 1
    end
end

local function storeQuestDialogueEvaluation(state)
    if type(state) ~= 'table' then
        return
    end

    latestQuestDialogueEvaluation = deepCopy(state)
    questDialogueEvaluationStats.received = questDialogueEvaluationStats.received + 1
    questDialogueEvaluationStats.topicCount = tonumber(state.topicCount or 0) or 0

    questDialogueEvaluationStats.authoritativelyAccepted = 0
    questDialogueEvaluationStats.fullyServerExecutable = 0
    questDialogueEvaluationStats.conditionRejected = 0
    questDialogueEvaluationStats.requiresFallback = 0
    questDialogueEvaluationStats.evaluatedConditions = 0
    questDialogueEvaluationStats.unsupportedConditions = 0
    questDialogueEvaluationStats.legacyEffectCount = 0
    questDialogueEvaluationStats.normalizedEffectCount = 0
    questDialogueEvaluationStats.legacyScriptCount = 0
    questDialogueEvaluationStats.plannedJournalEffects = 0
    questDialogueEvaluationStats.plannedTopicEffects = 0
    questDialogueEvaluationStats.plannedUnsupportedEffects = 0
    questDialogueEvaluationStats.unsupportedEffectCommandCount = 0

    if type(state.topics) ~= 'table' then
        return
    end

    for _, topic in ipairs(state.topics) do
        questDialogueEvaluationStats.authoritativelyAccepted = questDialogueEvaluationStats.authoritativelyAccepted
            + (tonumber(topic.authoritativelyAccepted or 0) or 0)
        questDialogueEvaluationStats.fullyServerExecutable = questDialogueEvaluationStats.fullyServerExecutable
            + (tonumber(topic.fullyServerExecutable or 0) or 0)
        questDialogueEvaluationStats.conditionRejected = questDialogueEvaluationStats.conditionRejected
            + (tonumber(topic.conditionRejected or 0) or 0)
        questDialogueEvaluationStats.requiresFallback = questDialogueEvaluationStats.requiresFallback
            + (tonumber(topic.requiresFallback or 0) or 0)
        questDialogueEvaluationStats.evaluatedConditions = questDialogueEvaluationStats.evaluatedConditions
            + (tonumber(topic.evaluatedConditions or 0) or 0)
        questDialogueEvaluationStats.unsupportedConditions = questDialogueEvaluationStats.unsupportedConditions
            + (tonumber(topic.unsupportedConditions or 0) or 0)
        questDialogueEvaluationStats.legacyEffectCount = questDialogueEvaluationStats.legacyEffectCount
            + (tonumber(topic.legacyEffectCount or 0) or 0)
        questDialogueEvaluationStats.normalizedEffectCount = questDialogueEvaluationStats.normalizedEffectCount
            + (tonumber(topic.normalizedEffectCount or 0) or 0)
        questDialogueEvaluationStats.legacyScriptCount = questDialogueEvaluationStats.legacyScriptCount
            + (tonumber(topic.legacyScriptCount or 0) or 0)
        questDialogueEvaluationStats.plannedJournalEffects = questDialogueEvaluationStats.plannedJournalEffects
            + (tonumber(topic.plannedJournalEffects or 0) or 0)
        questDialogueEvaluationStats.plannedTopicEffects = questDialogueEvaluationStats.plannedTopicEffects
            + (tonumber(topic.plannedTopicEffects or 0) or 0)
        questDialogueEvaluationStats.plannedUnsupportedEffects = questDialogueEvaluationStats.plannedUnsupportedEffects
            + (tonumber(topic.plannedUnsupportedEffects or 0) or 0)
        questDialogueEvaluationStats.unsupportedEffectCommandCount =
            questDialogueEvaluationStats.unsupportedEffectCommandCount
            + (tonumber(topic.unsupportedEffectCommandCount or 0) or 0)
    end
end

local function storeQuestDialogueChoice(state)
    if type(state) ~= 'table' then
        return
    end

    latestQuestDialogueChoice = deepCopy(state)
    questDialogueChoiceStats.received = questDialogueChoiceStats.received + 1
    questDialogueChoiceStats.responseCount = tonumber(state.responseCount or 0) or 0
    questDialogueChoiceStats.authoritativeCandidates = tonumber(state.authoritativeCandidates or 0) or 0
    questDialogueChoiceStats.fullyExecutable = state.selectedFullyExecutable == true and 1 or 0
    questDialogueChoiceStats.authoritativeApplyEnabled = state.authoritativeApplyEnabled == true
    questDialogueChoiceStats.applied = state.applied == true and 1 or 0
    questDialogueChoiceStats.plannedJournalEffects = tonumber(state.plannedJournalEffects or 0) or 0
    questDialogueChoiceStats.plannedTopicEffects = tonumber(state.plannedTopicEffects or 0) or 0
    questDialogueChoiceStats.plannedUnsupportedEffects = tonumber(state.plannedUnsupportedEffects or 0) or 0
    questDialogueChoiceStats.appliedEffects = tonumber(state.appliedEffects or 0) or 0
    questDialogueChoiceStats.skippedDuplicateEffects = tonumber(state.skippedDuplicateEffects or 0) or 0
end

local function getRuntimeStatus()
    if runtimeStatus == nil then
        return nil
    end

    local copy = shallowCopy(runtimeStatus)
    if type(runtimeStatus.capabilities) == 'table' then
        copy.capabilities = shallowCopy(runtimeStatus.capabilities)
    end
    if type(runtimeStatus.movementPolicy) == 'table' then
        copy.movementPolicy = shallowCopy(runtimeStatus.movementPolicy)
    end
    if type(runtimeStatus.questDatabase) == 'table' then
        copy.questDatabase = shallowCopy(runtimeStatus.questDatabase)
    end
    if type(runtimeStatus.questEventJournal) == 'table' then
        copy.questEventJournal = shallowCopy(runtimeStatus.questEventJournal)
    end
    if type(runtimeStatus.questRuntimeEvaluator) == 'table' then
        copy.questRuntimeEvaluator = shallowCopy(runtimeStatus.questRuntimeEvaluator)
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
    elseif event.serverEventName == 'movement_correction' and type(event.decodedPayload) == 'table' then
        storeMovementCorrection(event.decodedPayload)
    elseif event.serverEventName == 'player_packet_decision' and type(event.decodedPayload) == 'table' then
        storePlayerPacketDecision(event.decodedPayload)
    elseif event.serverEventName == 'quest_state' and type(event.decodedPayload) == 'table' then
        storeQuestState(event.decodedPayload)
    elseif event.serverEventName == 'quest_dialogue_evaluation' and type(event.decodedPayload) == 'table' then
        storeQuestDialogueEvaluation(event.decodedPayload)
    elseif event.serverEventName == 'quest_dialogue_choice' and type(event.decodedPayload) == 'table' then
        storeQuestDialogueChoice(event.decodedPayload)
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
        getLastMovementCorrection = function()
            return copyMovementCorrection(latestMovementCorrection)
        end,
        getMovementCorrectionStats = movementCorrectionStatsSnapshot,
        getLastPlayerPacketDecision = function(packetName)
            if type(packetName) ~= 'string' or packetName == '' then
                return nil
            end

            local state = latestPlayerPacketDecisionByPacket[packetName]
            if state == nil then
                return nil
            end
            return shallowCopy(state)
        end,
        getPlayerPacketDecisionStats = copyPlayerPacketDecisionStats,
        getQuestReadState = function()
            if latestQuestState == nil then
                return nil
            end
            return deepCopy(latestQuestState)
        end,
        getQuestStateStats = copyQuestStateStats,
        getQuestDialogueEvaluation = function()
            if latestQuestDialogueEvaluation == nil then
                return nil
            end
            return deepCopy(latestQuestDialogueEvaluation)
        end,
        getQuestDialogueEvaluationStats = copyQuestDialogueEvaluationStats,
        getQuestDialogueChoice = function()
            if latestQuestDialogueChoice == nil then
                return nil
            end
            return deepCopy(latestQuestDialogueChoice)
        end,
        getQuestDialogueChoiceStats = copyQuestDialogueChoiceStats,
        getRuntimeStatus = getRuntimeStatus,
        getRuntimeCapabilities = function()
            local state = getRuntimeStatus()
            if state == nil or type(state.capabilities) ~= 'table' then
                return nil
            end
            return shallowCopy(state.capabilities)
        end,
        getMovementPolicy = function()
            local state = getRuntimeStatus()
            if state == nil or type(state.movementPolicy) ~= 'table' then
                return nil
            end
            return shallowCopy(state.movementPolicy)
        end,
        getQuestDatabaseStatus = function()
            local state = getRuntimeStatus()
            if state == nil or type(state.questDatabase) ~= 'table' then
                return nil
            end
            return shallowCopy(state.questDatabase)
        end,
        getQuestEventJournalStatus = function()
            local state = getRuntimeStatus()
            if state == nil or type(state.questEventJournal) ~= 'table' then
                return nil
            end
            return shallowCopy(state.questEventJournal)
        end,
        getQuestRuntimeEvaluatorStatus = function()
            local state = getRuntimeStatus()
            if state == nil or type(state.questRuntimeEvaluator) ~= 'table' then
                return nil
            end
            return shallowCopy(state.questRuntimeEvaluator)
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
        getCellSnapshotReporter = getCellSnapshotReporter,
        getCellSnapshotReporterForCell = getCellSnapshotReporter,
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
        isLocalCellSnapshotReporter = function(cellOrDescription)
            local state = getCellSnapshotReporter(cellOrDescription)
            return state ~= nil and state.clientRole == 'snapshot-reporter'
        end,
        getCellSimulationOwner = function(cellOrDescription)
            local state = getCellAuthority(cellOrDescription)
            if state ~= nil and type(state.simulationOwner) == 'string' then
                return state.simulationOwner
            end

            local activity = getCellActivity(cellOrDescription)
            if activity ~= nil and type(activity.simulationOwner) == 'string' then
                return activity.simulationOwner
            end

            return nil
        end,
        isServerCellSimulationOwner = function(cellOrDescription)
            local state = getCellAuthority(cellOrDescription)
            if state ~= nil and state.serverOwnsSimulation == true then
                return true
            end

            local activity = getCellActivity(cellOrDescription)
            return activity ~= nil and activity.serverOwnsSimulation == true
        end,
    },
    engineHandlers = {
        onUpdate = function()
            dispatchServerEvents()
        end,
    },
}
