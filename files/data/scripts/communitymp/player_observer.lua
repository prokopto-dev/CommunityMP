local core = require('openmw.core')
local self = require('openmw.self')

local ok, communitymp = pcall(require, 'openmw.communitymp')
if not ok then
    return {}
end

local pollSeconds = 0.25
local movementHealthSeconds = 1.0
local elapsed = 0
local movementElapsed = 0
local frameCount = 0
local frameMinDt = nil
local frameMaxDt = 0
local lastCellKey = nil
local sentHello = false
local lastMovementHealthPosition = nil

local function jsonString(value)
    value = tostring(value or '')
    value = value:gsub('\\', '\\\\')
    value = value:gsub('"', '\\"')
    value = value:gsub('\b', '\\b')
    value = value:gsub('\f', '\\f')
    value = value:gsub('\n', '\\n')
    value = value:gsub('\r', '\\r')
    value = value:gsub('\t', '\\t')
    return '"' .. value .. '"'
end

local function jsonNumber(value)
    if type(value) ~= 'number' or value ~= value or value == math.huge or value == -math.huge then
        return 'null'
    end
    return string.format('%.3f', value)
end

local function jsonBool(value)
    return value and 'true' or 'false'
end

local function cellKey(cell)
    if not cell then
        return 'none'
    end

    if cell.isExterior then
        return table.concat({
            'exterior',
            cell.worldSpaceId or '',
            tostring(cell.gridX or 0),
            tostring(cell.gridY or 0),
        }, ':')
    end

    return 'interior:' .. tostring(cell.id or cell.name or '')
end

local function appendAll(parts, values)
    for _, value in ipairs(values) do
        parts[#parts + 1] = value
    end
end

local function appendPlayerSnapshot(parts, kind)
    local cell = self.cell
    local pos = self.position
    local key = cellKey(cell)

    appendAll(parts, {
        '"schema":', tostring(communitymp.BRIDGE_SCHEMA_VERSION), ',',
        '"kind":', jsonString(kind), ',',
        '"time":', jsonNumber(core.getSimulationTime()), ',',
        '"objectId":', jsonString(self.id), ',',
        '"cellKey":', jsonString(key), ',',
        '"cell":{',
            '"id":', jsonString(cell and cell.id or ''), ',',
            '"name":', jsonString(cell and cell.name or ''), ',',
            '"displayName":', jsonString(cell and cell.displayName or ''), ',',
            '"region":', jsonString(cell and cell.region or ''), ',',
            '"isExterior":', jsonBool(cell and cell.isExterior), ',',
            '"worldSpaceId":', jsonString(cell and cell.worldSpaceId or ''), ',',
            '"gridX":', jsonNumber(cell and cell.gridX), ',',
            '"gridY":', jsonNumber(cell and cell.gridY),
        '},',
        '"position":{',
            '"x":', jsonNumber(pos and pos.x), ',',
            '"y":', jsonNumber(pos and pos.y), ',',
            '"z":', jsonNumber(pos and pos.z),
        '}'
    })
end

local function buildPayload(kind)
    local parts = { '{' }
    appendPlayerSnapshot(parts, kind)
    parts[#parts + 1] = '}'
    return table.concat(parts)
end

local function sendObservation(kind)
    if not communitymp.isConnected() then
        return false
    end

    return communitymp.sendPlayerEvent('communitymp.player', kind, buildPayload(kind))
end

local function positionSnapshot(position)
    if position == nil then
        return nil
    end

    return { x = position.x or 0, y = position.y or 0, z = position.z or 0 }
end

local function distanceBetween(left, right)
    if left == nil or right == nil then
        return 0, 0
    end

    local dx = right.x - left.x
    local dy = right.y - left.y
    local dz = right.z - left.z
    local horizontal = math.sqrt(dx * dx + dy * dy)
    return math.sqrt(horizontal * horizontal + dz * dz), horizontal
end

local function buildMovementHealthPayload()
    local position = positionSnapshot(self.position)
    local distance, horizontalDistance = distanceBetween(lastMovementHealthPosition, position)
    local interval = math.max(movementElapsed, 0.001)
    local averageDt = frameCount > 0 and movementElapsed / frameCount or 0
    local estimatedHz = averageDt > 0 and 1 / averageDt or 0

    local parts = { '{' }
    appendPlayerSnapshot(parts, 'movement_health')
    parts[#parts + 1] = ','
    parts[#parts + 1] = '"timing":{'
    parts[#parts + 1] = '"observationInterval":'
    parts[#parts + 1] = jsonNumber(movementElapsed)
    parts[#parts + 1] = ',"frameCount":'
    parts[#parts + 1] = tostring(frameCount)
    parts[#parts + 1] = ',"averageDt":'
    parts[#parts + 1] = jsonNumber(averageDt)
    parts[#parts + 1] = ',"minDt":'
    parts[#parts + 1] = jsonNumber(frameMinDt or 0)
    parts[#parts + 1] = ',"maxDt":'
    parts[#parts + 1] = jsonNumber(frameMaxDt)
    parts[#parts + 1] = ',"estimatedHz":'
    parts[#parts + 1] = jsonNumber(estimatedHz)
    parts[#parts + 1] = '},"motion":{'
    parts[#parts + 1] = '"distance":'
    parts[#parts + 1] = jsonNumber(distance)
    parts[#parts + 1] = ',"horizontalDistance":'
    parts[#parts + 1] = jsonNumber(horizontalDistance)
    parts[#parts + 1] = ',"speed":'
    parts[#parts + 1] = jsonNumber(distance / interval)
    parts[#parts + 1] = ',"horizontalSpeed":'
    parts[#parts + 1] = jsonNumber(horizontalDistance / interval)
    parts[#parts + 1] = '}}'

    lastMovementHealthPosition = position
    return table.concat(parts)
end

local function sendMovementHealth()
    if not communitymp.isConnected() then
        return false
    end

    return communitymp.sendPlayerEvent('communitymp.player', 'movement_health', buildMovementHealthPayload())
end

local function resetMovementHealthCounters()
    movementElapsed = 0
    frameCount = 0
    frameMinDt = nil
    frameMaxDt = 0
end

local function observe()
    if not sentHello then
        sentHello = sendObservation('hello')
    end

    local key = cellKey(self.cell)
    if key ~= lastCellKey and sendObservation('cell_changed') then
        lastCellKey = key
    end

    if movementElapsed >= movementHealthSeconds and sendMovementHealth() then
        resetMovementHealthCounters()
    end
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            if dt <= 0 then
                return
            end

            elapsed = elapsed + dt
            movementElapsed = movementElapsed + dt
            frameCount = frameCount + 1
            frameMinDt = frameMinDt == nil and dt or math.min(frameMinDt, dt)
            frameMaxDt = math.max(frameMaxDt, dt)

            if elapsed < pollSeconds then
                return
            end

            elapsed = 0
            observe()
        end,
        onTeleported = function()
            if sendObservation('teleported') then
                lastCellKey = cellKey(self.cell)
            end
        end,
    },
}
