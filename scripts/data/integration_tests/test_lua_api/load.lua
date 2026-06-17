local content = require('openmw.content')

local function expectContentStore(name)
    assert(content[name] ~= nil, ('Expected openmw.content.%s to exist'):format(name))
    assert(content[name].records ~= nil, ('Expected openmw.content.%s.records to exist'):format(name))
end

local function expectNoContentStore(name)
    assert(content[name] == nil, ('Did not expect openmw.content.%s to exist'):format(name))
end

return {
    engineHandlers = {
        onContentFilesLoaded = function()
            for _, name in ipairs({
                'activators',
                'books',
                'doors',
                'ingredients',
                'lights',
                'miscs',
                'potions',
                'probes',
                'statics',
                'sounds',
                'gameSettings',
            }) do
                expectContentStore(name)
            end

            for _, name in ipairs({
                'activator',
                'book',
                'door',
                'ingredient',
                'light',
                'miscellaneous',
                'potion',
                'probe',
                'static',
                'sound',
                'gmsts',
            }) do
                expectNoContentStore(name)
            end

            content.statics.records.OMW_Generated_Static = { model = 'meshes/generatedonload.nif' }
            content.apparatuses.records.OMW_Generated_Apparatus = {
                name = 'Generated Apparatus',
                type = content.apparatuses.TYPE.MortarPestle,
                value = 35,
                weight = 4.5,
                quality = 2.25,
            }
            content.armors.records.OMW_Generated_Armor = {
                name = 'Generated Armor',
                type = content.armors.TYPE.Cuirass,
                value = 120,
                weight = 12.5,
                health = 80,
                baseArmor = 24,
                enchantCapacity = 6,
            }
            content.clothing.records.OMW_Generated_Clothing = {
                name = 'Generated Clothing',
                type = content.clothing.TYPE.Robe,
                value = 45,
                weight = 1.25,
                enchantCapacity = 4,
            }
            content.containers.records.OMW_Generated_Container = {
                name = 'Generated Container',
                weight = 250,
                isOrganic = false,
                isRespawning = true,
            }
            content.creatures.records.OMW_Generated_Creature = {
                name = 'Generated Creature',
                type = content.creatures.TYPE.Creatures,
                baseGold = 11,
                combatSkill = 25,
                magicSkill = 5,
                stealthSkill = 7,
                attack = { 1, 3, 2, 4, 1, 2 },
                canWalk = true,
                canSwim = false,
                isRespawning = true,
                isPersistent = true,
            }
            content.npcs.records.OMW_Generated_NPC = {
                name = 'Generated NPC',
                race = 'dark elf',
                class = 'warrior',
                isMale = true,
                isAutocalc = true,
                isRespawning = true,
                isPersistent = true,
                baseDisposition = 45,
                baseGold = 20,
            }
            content.weapons.records.OMW_Generated_Weapon = {
                name = 'Generated Weapon',
                type = content.weapons.TYPE.ShortBladeOneHand,
                value = 75,
                weight = 6,
                health = 90,
                speed = 1.2,
                reach = 1,
                enchantCapacity = 3,
                chopMinDamage = 3,
                chopMaxDamage = 9,
                slashMinDamage = 4,
                slashMaxDamage = 11,
                thrustMinDamage = 2,
                thrustMaxDamage = 7,
            }
        end
    }
}
