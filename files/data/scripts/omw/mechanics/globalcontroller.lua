local types = require('openmw.types')
local Lockable = types.Lockable
local Item = require('openmw.types').Item
local world = require('openmw.world')
local core = require('openmw.core')

local function onConsumeItem(data)
    local item = data.item
    local amount = data.amount
    if amount > item.count then
        print('Warning: tried to consume '..tostring(amount)..' '..tostring(item)..'s, but there were only '..tostring(item.count))
        amount = item.count
    end
    item:remove(amount)
end

local function onPlaySound3d(data)
    if data.sound then
        core.sound.playSound3d(data.sound, data.position, data.options)
    elseif data.file then
        core.sound.playSoundFile3d(data.file, data.position, data.options)
    end
end

local function onModifyItemCondition(data)
    local item = data.item
    local itemData = Item.itemData(item)
    local record = item.type.record(item)
    local maxCondition = record.health or record.maxCondition
    if not maxCondition then
        return
    end

    itemData.condition = math.min(maxCondition, math.max(0, itemData.condition + data.amount))

    if itemData.condition <= 0 and record.maxCondition then
        item:remove(1)
        return
    end

    -- Force unequip broken weapons/armor
    if data.actor and itemData.condition <= 0 then
        data.actor:sendEvent('Unequip', {item = item})
    end
end

return {
    eventHandlers = {
        SpawnVfx = function(data)
            world.vfx.spawn(data.model, data.position, data.options)
        end,
        PlaySound3d = onPlaySound3d,
        ConsumeItem = onConsumeItem,
        Lock = function(data)
            Lockable.lock(data.target, data.magnitude)
        end,
        Unlock = function(data)
            Lockable.unlock(data.target)
        end,
        ModifyItemCondition = onModifyItemCondition,
    },
}
