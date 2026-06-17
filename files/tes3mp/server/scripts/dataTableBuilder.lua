dataTableBuilder = {}

dataTableBuilder.BuildAIData = function(targetPid, targetUniqueIndex, action,
    posX, posY, posZ, distance, duration, shouldRepeat)

    local ai = {}
    ai.action = action
    ai.posX, ai.posY, ai.posZ = posX, posY, posZ
    ai.distance = distance
    ai.duration = duration
    ai.shouldRepeat = shouldRepeat

    if targetPid ~= nil and Players[targetPid] ~= nil then
        local targetPlayer = Players[targetPid]

        ai.targetPlayer = targetPlayer.accountName
        ai.targetAccountName = targetPlayer.accountName
        ai.targetCharacterName = targetPlayer.name

        if type(targetPlayer.GetCharacterStorageKey) == "function" then
            ai.targetPlayerKey = targetPlayer:GetCharacterStorageKey()
        end
    else
        ai.targetUniqueIndex = targetUniqueIndex
    end

    return ai
end

-- Use with logicHandler.CreateObject() functions
dataTableBuilder.BuildObjectData = function(refId, count, charge, enchantmentCharge, soul)

    local objectData = {}
    objectData.refId = refId
    objectData.count = count or 1
    objectData.charge = charge or -1
    objectData.enchantmentCharge = enchantmentCharge or -1
    objectData.soul = soul or ""

    return objectData
end

return dataTableBuilder
