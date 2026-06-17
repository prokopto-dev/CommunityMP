local core = require('openmw.core')

local ok, communitymp = pcall(require, 'openmw.communitymp')
if not ok then
    return {}
end

local function dispatchServerEvents()
    for _, event in ipairs(communitymp.receiveServerEvents()) do
        core.sendGlobalEvent('CommunityMPServerEvent', event)
    end
end

return {
    engineHandlers = {
        onUpdate = function()
            dispatchServerEvents()
        end,
    },
}
