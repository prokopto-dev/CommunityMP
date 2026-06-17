tableHelper = require("tableHelper")

guiHelper = {}
guiHelper.names = {"LOGIN", "REGISTER", "PLAYERSLIST", "CELLSLIST", "CHARACTERLIST"}
guiHelper.ID = tableHelper.enum(guiHelper.names)

local GetAccountName = function(pid)
    if Players ~= nil and Players[pid] ~= nil and Players[pid].accountName ~= nil then
        return Players[pid].accountName
    end

    return tes3mp.GetName(pid)
end

guiHelper.ShowLogin = function(pid)
    local accountName = GetAccountName(pid)
    tes3mp.PasswordDialog(pid, guiHelper.ID.LOGIN, "Account password:",
        "Account: " .. accountName .. "\n" ..
        "Sign in with this server's account password. Your character name is separate and is loaded from your save.")
end

guiHelper.ShowRegister = function(pid)
    local accountName = GetAccountName(pid)
    tes3mp.PasswordDialog(pid, guiHelper.ID.REGISTER, "Create account password:",
        "Account: " .. accountName .. "\n" ..
        "Use a unique password for this server. After registration, character creation will ask for your in-game name.")
end

guiHelper.ShowCharacterList = function(pid)
    if Players == nil or Players[pid] == nil then
        return
    end

    local characterCount = Players[pid]:GetCharacterSlotCount()
    local list = {}
    local metadata = {}

    for characterIndex = 1, characterCount do
        if Players[pid].GetCharacterSlotListLabel ~= nil then
            table.insert(list, Players[pid]:GetCharacterSlotListLabel(characterIndex))
        else
            table.insert(list, Players[pid]:GetCharacterSlotName(characterIndex))
        end

        if Players[pid].GetCharacterSlotPreviewMetadata ~= nil then
            table.insert(metadata, Players[pid]:GetCharacterSlotPreviewMetadata(characterIndex))
        else
            table.insert(metadata, "")
        end
    end

    table.insert(list, "+ Create new character")
    table.insert(metadata, "")

    local label = "Choose a character for account " .. GetAccountName(pid) .. "\n" ..
        "Character slots keep separate inventory, journal, topics and quest state."
    local items = table.concat(list, "\n")
    local previewMetadata = table.concat(metadata, "\n")

    if tes3mp.ListBoxWithMetadata ~= nil then
        tes3mp.ListBoxWithMetadata(pid, guiHelper.ID.CHARACTERLIST, label, items, previewMetadata)
    else
        tes3mp.ListBox(pid, guiHelper.ID.CHARACTERLIST, label, items)
    end
end

local GetConnectedPlayerList = function()

    local lastPid = tes3mp.GetLastPlayerId()
    local list = ""
    local divider = ""

    for playerIndex = 0, lastPid do
        if playerIndex == lastPid then
            divider = ""
        else
            divider = "\n"
        end
        if Players[playerIndex] ~= nil and Players[playerIndex]:IsLoggedIn() then

            list = list .. tostring(Players[playerIndex].name) .. " (pid: " .. tostring(Players[playerIndex].pid) ..
                ", ping: " .. tostring(tes3mp.GetAvgPing(Players[playerIndex].pid)) .. ")" .. divider
        end
    end

    return list
end

local GetLoadedCellList = function()
    local list = ""
    local divider = ""

    local cellCount = logicHandler.GetLoadedCellCount()
    local cellIndex = 0

    for key, value in pairs(LoadedCells) do
        cellIndex = cellIndex + 1

        if cellIndex == cellCount then
            divider = ""
        else
            divider = "\n"
        end

        list = list .. key .. " (auth: " .. LoadedCells[key]:GetAuthority() .. ", loaded by " ..
            LoadedCells[key]:GetVisitorCount() .. ")" .. divider
    end

    return list
end

local GetLoadedRegionList = function()
    local list = ""
    local divider = ""

    local regionCount = logicHandler.GetLoadedRegionCount()
    local regionIndex = 0

    for key, value in pairs(WorldInstance.storedRegions) do
        local visitorCount = WorldInstance:GetRegionVisitorCount(key)

        if visitorCount > 0 then
            regionIndex = regionIndex + 1

            if regionIndex == regionCount then
                divider = ""
            else
                divider = "\n"
            end

            list = list .. key .. " (auth: " .. WorldInstance:GetRegionAuthority(key) .. ", loaded by " ..
                visitorCount .. ")" .. divider
        end
    end

    return list
end

local GetPlayerInventoryList = function(pid)

    local list = ""
    local divider = ""
    local lastItemIndex = tableHelper.getCount(Players[pid].data.inventory)

    for index, currentItem in ipairs(Players[pid].data.inventory) do

        if index == lastItemIndex then
            divider = ""
        else
            divider = "\n"
        end

        list = list .. index .. ": " .. currentItem.refId .. " (count: " .. currentItem.count .. ")" .. divider
    end

    return list
end

guiHelper.ShowPlayerList = function(pid)

    local playerCount = logicHandler.GetConnectedPlayerCount()
    local label = playerCount .. " connected player"

    if playerCount ~= 1 then
        label = label .. "s"
    end

    tes3mp.ListBox(pid, guiHelper.ID.PLAYERSLIST, label, GetConnectedPlayerList())
end

guiHelper.ShowCellList = function(pid)

    local cellCount = logicHandler.GetLoadedCellCount()
    local label = cellCount .. " loaded cell"

    if cellCount ~= 1 then
        label = label .. "s"
    end

    tes3mp.ListBox(pid, guiHelper.ID.CELLSLIST, label, GetLoadedCellList())
end

guiHelper.ShowRegionList = function(pid)

    local regionCount = logicHandler.GetLoadedRegionCount()
    local label = regionCount .. " loaded region"

    if regionCount ~= 1 then
        label = label .. "s"
    end

    tes3mp.ListBox(pid, guiHelper.ID.CELLSLIST, label, GetLoadedRegionList())
end

guiHelper.ShowInventoryList = function(menuId, pid, inventoryPid)

    local inventoryCount = tableHelper.getCount(Players[pid].data.inventory)
    local label = inventoryCount .. " item"

    if inventoryCount ~= 1 then
        label = label .. "s"
    end

    tes3mp.ListBox(pid, menuId, label, GetPlayerInventoryList(inventoryPid))
end

return guiHelper
