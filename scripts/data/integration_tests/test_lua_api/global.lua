local testing = require('testing_util')
local core = require('openmw.core')
local async = require('openmw.async')
local util = require('openmw.util')
local types = require('openmw.types')
local vfs = require('openmw.vfs')
local world = require('openmw.world')
local I = require('openmw.interfaces')

testing.registerGlobalTest('crash in lua coroutine when accessing type (#8757)', function()
    local co = coroutine.wrap(function()
        testing.expectEqual(tostring(world.players[1].type), 'Player')
    end)
    co()
end)

testing.registerGlobalTest('timers', function()
    testing.expectAlmostEqual(core.getGameTimeScale(), 30, 'incorrect getGameTimeScale() result')
    testing.expectAlmostEqual(core.getSimulationTimeScale(), 1, 'incorrect getSimulationTimeScale result')

    local startGameTime = core.getGameTime()
    local startSimulationTime = core.getSimulationTime()

    local ts1, ts2, th1, th2
    local cb = async:registerTimerCallback("tfunc", function(arg)
        if arg == 'g' then
            th1 = core.getGameTime() - startGameTime
        else
            ts1 = core.getSimulationTime() - startSimulationTime
        end
    end)
    async:newGameTimer(36, cb, 'g')
    async:newSimulationTimer(0.5, cb, 's')
    async:newUnsavableGameTimer(72, function()
        th2 = core.getGameTime() - startGameTime
    end)
    async:newUnsavableSimulationTimer(1, function()
        ts2 = core.getSimulationTime() - startSimulationTime
    end)

    while not (ts1 and ts2 and th1 and th2) do
        coroutine.yield()
    end

    testing.expectGreaterOrEqual(th1, 36, 'async:newGameTimer failed')
    testing.expectGreaterOrEqual(ts1, 0.5, 'async:newSimulationTimer failed')
    testing.expectGreaterOrEqual(th2, 72, 'async:newUnsavableGameTimer failed')
    testing.expectGreaterOrEqual(ts2, 1, 'async:newUnsavableSimulationTimer failed')
end)

testing.registerGlobalTest('teleport', function()
    local player = world.players[1]
    player:teleport('', util.vector3(100, 50, 500), util.transform.rotateZ(math.rad(90)))
    coroutine.yield()
    testing.expect(player.cell.isExterior, 'teleport to exterior failed')
    testing.expectEqualWithDelta(player.position.x, 100, 1, 'incorrect position after teleporting')
    testing.expectEqualWithDelta(player.position.y, 50, 1, 'incorrect position after teleporting')
    testing.expectEqualWithDelta(player.position.z, 500, 1, 'incorrect position after teleporting')
    testing.expectEqualWithDelta(player.rotation:getYaw(), math.rad(90), 0.05, 'incorrect yaw rotation after teleporting')
    testing.expectEqualWithDelta(player.rotation:getPitch(), math.rad(0), 0.05, 'incorrect pitch rotation after teleporting')

    local rotationX1, rotationZ1 = player.rotation:getAnglesXZ()
    testing.expectEqualWithDelta(rotationX1, math.rad(0), 0.05, 'incorrect x rotation from getAnglesXZ after teleporting')
    testing.expectEqualWithDelta(rotationZ1, math.rad(90), 0.05, 'incorrect z rotation from getAnglesXZ after teleporting')

    local rotationZ2, rotationY2, rotationX2 = player.rotation:getAnglesZYX()
    testing.expectEqualWithDelta(rotationZ2, math.rad(90), 0.05, 'incorrect z rotation from getAnglesZYX after teleporting')
    testing.expectEqualWithDelta(rotationY2, math.rad(0), 0.05, 'incorrect y rotation from getAnglesZYX after teleporting')
    testing.expectEqualWithDelta(rotationX2, math.rad(0), 0.05, 'incorrect x rotation from getAnglesZYX after teleporting')

    player:teleport('', player.position, {rotation=util.transform.rotateZ(math.rad(-90)), onGround=true})
    coroutine.yield()
    testing.expectEqualWithDelta(player.rotation:getYaw(), math.rad(-90), 0.05, 'options.rotation is not working')
    testing.expectLessOrEqual(player.position.z, 400, 'options.onGround is not working')

    player:teleport('', util.vector3(50, -100, 0))
    coroutine.yield()
    testing.expect(player.cell.isExterior, 'teleport to exterior failed')
    testing.expectEqualWithDelta(player.position.x, 50, 1, 'incorrect position after teleporting')
    testing.expectEqualWithDelta(player.position.y, -100, 1, 'incorrect position after teleporting')
    testing.expectEqualWithDelta(player.rotation:getYaw(), math.rad(-90), 0.05, 'teleporting changes rotation')
end)

testing.registerGlobalTest('getGMST', function()
    testing.expectEqual(core.getGMST('non-existed gmst'), nil)
    testing.expectEqual(core.getGMST('Water_RippleFrameCount'), 4)
    testing.expectEqual(core.getGMST('Inventory_DirectionalDiffuseR'), 0.5)
    testing.expectEqual(core.getGMST('Level_Up_Level2'), 'something')
end)

testing.registerGlobalTest('lua log levels', function()
    testing.expectEqual(core.LOG_LEVEL.Error, 1)
    testing.expectEqual(core.LOG_LEVEL.Warning, 2)
    testing.expectEqual(core.LOG_LEVEL.Info, 3)
    testing.expectEqual(core.LOG_LEVEL.Verbose, 4)
    testing.expectEqual(core.LOG_LEVEL.Debug, 5)

    core.log(core.LOG_LEVEL.Warning, 'warning from lua api test', 42)
    core.log(core.LOG_LEVEL.Verbose, 'verbose from lua api test')
    core.log(core.LOG_LEVEL.Debug, 'debug from lua api test')

    local ok, err = pcall(function() core.log(0, 'invalid') end)
    testing.expectEqual(ok, false)
    testing.expectEqual(err, 'Invalid log level')
end)

testing.registerGlobalTest('MWScript', function()
    local variableStoreCount = 19
    local variableStore = world.mwscript.getGlobalVariables(player)
    testing.expectEqual(variableStoreCount, #variableStore)

    variableStore.year = 5
    testing.expectEqual(5, variableStore.year)
    variableStore.year = 1
    local indexCheck = 0
    for index, value in ipairs(variableStore) do
        testing.expectEqual(variableStore[index], value)
        indexCheck = indexCheck + 1
    end
    testing.expectEqual(variableStoreCount, indexCheck)
    indexCheck = 0
    for index, value in pairs(variableStore) do
        testing.expectEqual(variableStore[index], value)
        indexCheck = indexCheck + 1
    end
    testing.expectEqual(variableStoreCount, indexCheck)
end)

local function testRecordStore(store, storeName, skipPairs)
    testing.expect(store.records)
    local firstRecord = store.records[1]
    if not firstRecord then
        return
    end
    testing.expectEqual(firstRecord.id, store.records[firstRecord.id].id)
    local status, _ = pcall(function()
        for index, value in ipairs(store.records) do
            if value.id == firstRecord.id then
                testing.expectEqual(index, 1, storeName)
                break
            end
        end
    end)

    testing.expectEqual(status, true, storeName)
end

testing.registerGlobalTest('record stores', function()
    for key, type in pairs(types) do
        if type.records then
            testRecordStore(type, key)
        end
    end
    testRecordStore(core.magic.enchantments, "enchantments")
    testRecordStore(core.magic.effects, "effects", true)
    testRecordStore(core.magic.spells, "spells")

    testRecordStore(core.stats.Attribute, "Attribute")
    testRecordStore(core.stats.Skill, "Skill")

    testRecordStore(core.sound, "sound")
    testRecordStore(core.factions, "factions")
    testRecordStore(core.regions, "regions")
    testRecordStore(core.dialogue.greeting, "dialogue greeting")
    testRecordStore(core.dialogue.topic, "dialogue topic")
    testRecordStore(core.dialogue.journal, "dialogue journal")
    testRecordStore(core.dialogue.persuasion, "dialogue persuasion")
    testRecordStore(core.dialogue.voice, "dialogue voice")

    testRecordStore(types.NPC.classes, "classes")
    testRecordStore(types.NPC.races, "races")
    testRecordStore(types.Player.birthSigns, "birthSigns")
end)

testing.registerGlobalTest('record creation', function()
    local newLight = {
        isCarriable = true,
        isDynamic = true,
        isFire =false,
        isFlicker = false,
        isFlickerSlow = false,
        isNegative = false,
        isOffByDefault = false,
        isPulse = false,
        weight = 1,
        value = 10,
        duration = 12,
        radius = 30,
        color = util.color.hex('123456'),
        name = "TestLight",
        model = "meshes/marker_door.dae"
    }
    local draft = types.Light.createRecordDraft(newLight)
    local record = world.createRecord(draft)
    for key, value in pairs(newLight) do
        testing.expectEqual(record[key], value)
    end

    local newApparatus = {
        name = "TestApparatus",
        model = "meshes/marker_door.dae",
        mwscript = "",
        icon = "icons/tx_apparatus_01.dds",
        type = types.Apparatus.TYPE.Retort,
        weight = 6.25,
        value = 115,
        quality = 3.5,
    }
    record = world.createRecord(types.Apparatus.createRecordDraft(newApparatus))
    for key, value in pairs(newApparatus) do
        testing.expectEqual(record[key], value)
    end

    local newLockpick = {
        name = "TestLockpick",
        model = "meshes/marker_door.dae",
        mwscript = "",
        icon = "icons/tx_lockpick_01.dds",
        maxCondition = 12,
        weight = 1.5,
        value = 35,
        quality = 2.25,
    }
    record = world.createRecord(types.Lockpick.createRecordDraft(newLockpick))
    for key, value in pairs(newLockpick) do
        testing.expectEqual(record[key], value)
    end

    local newRepair = {
        name = "TestRepair",
        model = "meshes/marker_door.dae",
        mwscript = "",
        icon = "icons/tx_repair_01.dds",
        maxCondition = 8,
        weight = 2.5,
        value = 42,
        quality = 1.75,
    }
    record = world.createRecord(types.Repair.createRecordDraft(newRepair))
    for key, value in pairs(newRepair) do
        testing.expectEqual(record[key], value)
    end

    local newIngredient = {
        name = "TestIngredient",
        model = "meshes/marker_door.dae",
        mwscript = "",
        icon = "icons/tx_ingredient_01.dds",
        weight = 0.2,
        value = 4,
        effects = {
            { id = "restore health", affectedAttribute = "", affectedSkill = "" },
            { id = "restore fatigue", affectedAttribute = "", affectedSkill = "" },
        },
    }
    record = world.createRecord(types.Ingredient.createRecordDraft(newIngredient))
    testing.expectEqual(record.name, newIngredient.name)
    testing.expectEqual(record.model, newIngredient.model)
    testing.expectEqual(record.mwscript, newIngredient.mwscript)
    testing.expectEqual(record.icon, newIngredient.icon)
    testing.expectEqual(record.weight, newIngredient.weight)
    testing.expectEqual(record.value, newIngredient.value)
    for index, effect in ipairs(newIngredient.effects) do
        testing.expectEqual(record.effects[index].id, effect.id)
        testing.expectEqual(record.effects[index].affectedAttribute, effect.affectedAttribute)
        testing.expectEqual(record.effects[index].affectedSkill, effect.affectedSkill)
    end
    local skillTemplateIngredient = types.Ingredient.createRecordDraft({
        name = "SkillTemplateIngredient",
        model = "meshes/marker_door.dae",
        mwscript = "",
        icon = "icons/tx_ingredient_01.dds",
        weight = 0.2,
        value = 4,
        effects = {
            { id = "fortify skill", affectedSkill = "alchemy" },
        },
    })
    local patchedIngredient = types.Ingredient.createRecordDraft({
        template = skillTemplateIngredient,
        effects = {
            { id = "restore fatigue" },
        },
    })
    testing.expectEqual(patchedIngredient.effects[1].id, "restore fatigue")
    testing.expectEqual(patchedIngredient.effects[1].affectedAttribute, "")
    testing.expectEqual(patchedIngredient.effects[1].affectedSkill, "")

    local newSound = {
        fileName = "sound/test.wav",
        volume = 220,
        minRange = 10,
        maxRange = 40,
    }
    record = world.createRecord(core.sound.createRecordDraft(newSound))
    for key, value in pairs(newSound) do
        testing.expectEqual(record[key], value)
    end
end)

testing.registerGlobalTest('UTF-8 characters', function()
    testing.expectEqual(utf8.codepoint("😀"), 0x1F600)

    local chars = {}

    for codepoint = 0, 0x10FFFF do
        local char = utf8.char(codepoint)
        local charSize = string.len(char)

        testing.expect(not chars[char], nil, "Duplicate UTF-8 character: " .. char)
        chars[char] = true

        if codepoint <= 0x7F then
            testing.expectEqual(charSize, 1)
        elseif codepoint <= 0x7FF then
            testing.expectEqual(charSize, 2)
        elseif codepoint <= 0xFFFF then
            testing.expectEqual(charSize, 3)
        elseif codepoint <= 0x10FFFF then
            testing.expectEqual(charSize, 4)
        end

        testing.expectEqual(utf8.codepoint(char), codepoint)
        testing.expectEqual(utf8.len(char), 1)
    end
end)

testing.registerGlobalTest('UTF-8 strings', function()
    local utf8str = "Hello, 你好, 🌎!"

    local str = ""
    for utf_char in utf8str:gmatch(utf8.charpattern) do
        str = str .. utf_char
    end
    testing.expectEqual(str, utf8str)

    testing.expectEqual(utf8.len(utf8str), 13)
    testing.expectEqual(utf8.offset(utf8str, 9), 11)
end)

testing.registerGlobalTest('memory limit', function()
    local ok, err = pcall(function()
        local t = {}
        local n = 1
        while true do
            t[n] = n
            n = n + 1
        end
    end)
    testing.expectEqual(ok, false, 'Script reaching memory limit should fail')
    testing.expectEqual(err, 'not enough memory')
end)

local function initPlayer()
    local player = world.players[1]
    player:teleport('', util.vector3(4096, 4096, 1745), util.transform.identity)
    coroutine.yield()
    return player
end

testing.registerGlobalTest('vfs', function()
    local file = 'test_vfs_dir/lines.txt'
    local nosuchfile = 'test_vfs_dir/nosuchfile'
    testing.expectEqual(vfs.fileExists(file), true, 'lines.txt should exist')
    testing.expectEqual(vfs.fileExists(nosuchfile), false, 'nosuchfile should not exist')

    local expectedLines = { '1', '2', '', '4' }
    local getLine = vfs.lines(file)
    for _,v in pairs(expectedLines) do
        testing.expectEqual(getLine(), v)
    end
    testing.expectEqual(getLine(), nil, 'All lines should have been read')
    local ok = pcall(function()
        vfs.lines(nosuchfile)
    end)
    testing.expectEqual(ok, false, 'Should not be able to read lines from nonexistent file')

    local getPath = vfs.pathsWithPrefix('test_vfs_dir/')
    testing.expectEqual(getPath(), file)
    testing.expectEqual(getPath(), nil, 'All paths should have been read')

    local handle = vfs.open(file)
    testing.expectEqual(vfs.type(handle), 'file', 'File should be open')
    testing.expectEqual(handle.fileName, file)

    local n1, n2, _, l3, l4 = handle:read("*n", "*number", "*l", "*line", "*l")
    testing.expectEqual(n1, 1)
    testing.expectEqual(n2, 2)
    testing.expectEqual(l3, '')
    testing.expectEqual(l4, '4')

    testing.expectEqual(handle:seek('set', 0), 0, 'Reading should happen from the start of the file')
    testing.expectEqual(handle:read("*a"), '1\n2\n\n4')

    testing.expectEqual(handle:close(), true, 'File should be closeable')
    testing.expectEqual(vfs.type(handle), 'closed file', 'File should be closed')

    handle = vfs.open(nosuchfile)
    testing.expectEqual(handle, nil, 'vfs.open should return nil on nonexistent files')

    getLine = vfs.open(file):lines()
    for _,v in pairs(expectedLines) do
        testing.expectEqual(getLine(), v)
    end
end)

testing.registerGlobalTest('commit crime', function()
    local player = initPlayer()
    testing.expectEqual(player == nil, false, 'A viable player reference should exist to run `commit crime`')
    testing.expectEqual(I.Crimes == nil, false, 'Crimes interface should be available in global contexts')

    -- Reset crime level to have a clean slate
    types.Player.setCrimeLevel(player, 0)
    testing.expectEqual(I.Crimes.commitCrime(player, { type = types.Player.OFFENSE_TYPE.Theft, victim = player, arg = 100}).wasCrimeSeen, false, "Running the crime with the player as the victim should not result in a seen crime")
    testing.expectEqual(I.Crimes.commitCrime(player, { type = types.Player.OFFENSE_TYPE.Theft, arg = 50 }).wasCrimeSeen, false, "Running the crime with no victim and a type shouldn't raise errors")
    testing.expectEqual(I.Crimes.commitCrime(player, { type = types.Player.OFFENSE_TYPE.Murder }).wasCrimeSeen, false, "Running a murder crime should work even without a victim")

    -- Create a mockup target for crimes
    local victim = world.createObject(types.NPC.record(player).id)
    victim:teleport(player.cell, player.position + util.vector3(0, 300, 0))
    coroutine.yield()

    -- Reset crime level for testing with a valid victim
    types.Player.setCrimeLevel(player, 0)
    testing.expectEqual(I.Crimes.commitCrime(player, { victim = victim, type = types.Player.OFFENSE_TYPE.Theft, arg = 50 }).wasCrimeSeen, true, "Running a crime with a valid victim should notify them when the player is not sneaking, even if it's not explicitly passed in")
    testing.expectEqual(types.Player.getCrimeLevel(player), 0, "Crime level should not change if the victim's alarm value is low and there's no other witnesses")
end)

testing.registerGlobalTest('record model property', function()
    local player = world.players[1]
    testing.expectEqual(types.NPC.record(player).model, 'meshes/basicplayer.dae')
end)

testing.registerGlobalTest('npc breath timer', function()
    local player = world.players[1]
    types.NPC.setBreathTimer(player, 7.25)
    testing.expectEqual(types.NPC.getBreathTimer(player), 7.25)

    local ok, err = pcall(function() types.NPC.setBreathTimer(player, math.huge) end)
    testing.expectEqual(ok, false)
    testing.expectEqual(err, 'Value must be a finite number')
end)

local function registerPlayerTest(name)
    testing.registerGlobalTest(name, function()
        local player = initPlayer()
        testing.runLocalTest(player, name)
    end)
end

registerPlayerTest('player yaw rotation')
registerPlayerTest('player pitch rotation')
registerPlayerTest('player pitch and yaw rotation')
registerPlayerTest('player rotation')
registerPlayerTest('player forward running')
registerPlayerTest('player diagonal walking')
registerPlayerTest('findPath')
registerPlayerTest('findPath with checkpoints')
registerPlayerTest('findRandomPointAroundCircle')
registerPlayerTest('castNavigationRay')
registerPlayerTest('findNearestNavMeshPosition')
registerPlayerTest('player memory limit')
registerPlayerTest('queued UI mode is reflected immediately')
registerPlayerTest('ui drag and drop state is exposed')

testing.registerGlobalTest('player weapon attack', function()
    local player = initPlayer()
    world.createObject('basic_dagger1h', 1):moveInto(player)
    testing.runLocalTest(player, 'player weapon attack')
end)

testing.registerGlobalTest('load while teleporting - init player', function()
    local player = world.players[1]
    player:teleport('Museum of Wonders', util.vector3(0, -1500, 111), util.transform.rotateZ(math.rad(180)))
end)

testing.registerGlobalTest('load while teleporting - teleport', function()
    local player = world.players[1]
    local landracer = world.createObject('landracer')
    landracer:teleport(player.cell, player.position + util.vector3(0, 500, 0))
    coroutine.yield()

    local door = world.getObjectByFormId(core.getFormId('the_hub.omwaddon', 26))
    door:activateBy(player)
    coroutine.yield()

    landracer:teleport(player.cell, player.position)
end)

testing.registerGlobalTest('nan float', function()
    local nan = 0.0 / 0.0
    local ok, err = pcall(function() world.setGameTimeScale(nan) end)
    testing.expectEqual(ok, false)
    testing.expectEqual(err, 'Value must be a finite number')
end)

testing.registerGlobalTest('nan vector', function()
    local nan = 0.0 / 0.0
    local ok, err = pcall(function() core.weather.records[1].stormDirection = util.vector3(nan, nan, nan) end)
    testing.expectEqual(ok, false)
    testing.expectEqual(err, 'Vector must contain finite numbers')
end)

testing.registerGlobalTest('mwscript magic interactions', function()
    local player = world.players[1]
    local script = world.mwscript.getGlobalScript('OpenMW_Tests', player)
    testing.expectEqual(script == nil, false, 'Expected mwscript OpenMW_Tests to be active')
    script.variables.state = 1
    local globals = world.mwscript.getGlobalVariables(player);
    globals.OpenMW_Tests_Failed = 0
    while script.variables.state ~= 0 and globals.OpenMW_Tests_Failed == 0 and script.isRunning do
        coroutine.yield()
    end
    testing.expectEqual(script.isRunning, true, 'OpenMW_Tests should not crash')
    testing.expectEqual(globals.OpenMW_Tests_Failed, 0, 'OpenMW_Tests should run without issue')
end)

testing.registerGlobalTest('load script generated static', function()
    local record = types.Static.records.OMW_Generated_Static
    testing.expectNotEqual(record, nil, 'OMW_Generated_Static should have been generated')
    testing.expectEqual(record.model, 'meshes/generatedonload.nif')
end)

testing.registerGlobalTest('load script generated content records', function()
    local apparatus = types.Apparatus.records.OMW_Generated_Apparatus
    testing.expectNotEqual(apparatus, nil, 'OMW_Generated_Apparatus should have been generated')
    testing.expectEqual(apparatus.name, 'Generated Apparatus')
    testing.expectEqual(apparatus.type, types.Apparatus.TYPE.MortarPestle)
    testing.expectAlmostEqual(apparatus.quality, 2.25)

    local armor = types.Armor.records.OMW_Generated_Armor
    testing.expectNotEqual(armor, nil, 'OMW_Generated_Armor should have been generated')
    testing.expectEqual(armor.name, 'Generated Armor')
    testing.expectEqual(armor.type, types.Armor.TYPE.Cuirass)
    testing.expectEqual(armor.baseArmor, 24)
    testing.expectAlmostEqual(armor.enchantCapacity, 6)

    local clothing = types.Clothing.records.OMW_Generated_Clothing
    testing.expectNotEqual(clothing, nil, 'OMW_Generated_Clothing should have been generated')
    testing.expectEqual(clothing.name, 'Generated Clothing')
    testing.expectEqual(clothing.type, types.Clothing.TYPE.Robe)
    testing.expectAlmostEqual(clothing.enchantCapacity, 4)

    local container = types.Container.records.OMW_Generated_Container
    testing.expectNotEqual(container, nil, 'OMW_Generated_Container should have been generated')
    testing.expectEqual(container.name, 'Generated Container')
    testing.expectEqual(container.isOrganic, false)
    testing.expectEqual(container.isRespawning, true)

    local creature = types.Creature.records.OMW_Generated_Creature
    testing.expectNotEqual(creature, nil, 'OMW_Generated_Creature should have been generated')
    testing.expectEqual(creature.name, 'Generated Creature')
    testing.expectEqual(creature.type, types.Creature.TYPE.Creatures)
    testing.expectEqual(creature.attack[2], 3)
    testing.expectEqual(creature.canWalk, true)
    testing.expectEqual(creature.isRespawning, true)
    testing.expectEqual(creature.isPersistent, true)

    local npc = types.NPC.records.OMW_Generated_NPC
    testing.expectNotEqual(npc, nil, 'OMW_Generated_NPC should have been generated')
    testing.expectEqual(npc.name, 'Generated NPC')
    testing.expectEqual(npc.race, 'dark elf')
    testing.expectEqual(npc.class, 'warrior')
    testing.expectEqual(npc.isMale, true)
    testing.expectEqual(npc.isAutocalc, true)
    testing.expectEqual(npc.isRespawning, true)
    testing.expectEqual(npc.isPersistent, true)
    testing.expectEqual(npc.baseDisposition, 45)
    testing.expectEqual(npc.baseGold, 20)

    local weapon = types.Weapon.records.OMW_Generated_Weapon
    testing.expectNotEqual(weapon, nil, 'OMW_Generated_Weapon should have been generated')
    testing.expectEqual(weapon.name, 'Generated Weapon')
    testing.expectEqual(weapon.type, types.Weapon.TYPE.ShortBladeOneHand)
    testing.expectEqual(weapon.chopMaxDamage, 9)
    testing.expectEqual(weapon.slashMaxDamage, 11)
    testing.expectEqual(weapon.thrustMaxDamage, 7)
end)

return {
    engineHandlers = {
        onUpdate = testing.updateGlobal,
    },
    eventHandlers = testing.globalEventHandlers,
}
