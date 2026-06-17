local core = require('openmw.core')
local self = require('openmw.self')

local ok, communitymp = pcall(require, 'openmw.communitymp')
if not ok then
    return {}
end

local pollSeconds = 0.25
local elapsed = 0
local lastCellKey = nil
local sentHello = false

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
    if type(value) ~= 'number' then
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

local function buildPayload(kind)
    local cell = self.cell
    local pos = self.position
    local key = cellKey(cell)

    return table.concat({
        '{',
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
        '}',
        '}',
    })
end

local function sendObservation(kind)
    if not communitymp.isConnected() then
        return false
    end

    return communitymp.sendPlayerEvent('communitymp.player', kind, buildPayload(kind))
end

local function observe()
    if not sentHello then
        sentHello = sendObservation('hello')
    end

    local key = cellKey(self.cell)
    if key ~= lastCellKey and sendObservation('cell_changed') then
        lastCellKey = key
    end
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            if dt <= 0 then
                return
            end

            elapsed = elapsed + dt
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
