#include "types.hpp"

#include "../contentbindings.hpp"
#include "../stats.hpp"
#include "actor.hpp"
#include "modelproperty.hpp"
#include "usertypeutil.hpp"

#include <components/esm3/loadcrea.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/util.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

namespace
{
    ESM::Creature tableToCreatureImpl(const sol::table& rec)
    {
        auto crea = MWLua::Types::initFromTemplate<ESM::Creature>(rec);
        auto setCreatureFlag = [&](std::string_view key, int flag) {
            if (rec[key] == sol::nil)
                return;

            bool value = rec[key];
            if (value)
                crea.mFlags |= flag;
            else
                crea.mFlags &= ~flag;
        };

        // Basic fields
        if (rec["name"] != sol::nil)
            crea.mName = rec["name"];
        if (rec["model"] != sol::nil)
            crea.mModel = Misc::ResourceHelpers::meshPathForESM3(rec["model"].get<std::string_view>());
        if (rec["mwscript"] != sol::nil)
            crea.mScript = ESM::RefId::deserializeText(rec["mwscript"].get<std::string_view>());
        if (rec["baseCreature"] != sol::nil)
            crea.mOriginal = ESM::RefId::deserializeText(rec["baseCreature"].get<std::string_view>());

        if (rec["soulValue"] != sol::nil)
            crea.mData.mSoul = rec["soulValue"].get<int>();
        if (rec["type"] != sol::nil)
            crea.mData.mType = rec["type"].get<int>();
        if (rec["baseGold"] != sol::nil)
            crea.mData.mGold = rec["baseGold"].get<int>();
        if (rec["combatSkill"] != sol::nil)
            crea.mData.mCombat = rec["combatSkill"].get<int>();
        if (rec["magicSkill"] != sol::nil)
            crea.mData.mMagic = rec["magicSkill"].get<int>();
        if (rec["stealthSkill"] != sol::nil)
            crea.mData.mStealth = rec["stealthSkill"].get<int>();

        if (rec["attack"] != sol::nil)
        {
            const sol::table atk = rec["attack"];
            for (int i = 0; i < 6; ++i)
            {
                sol::object v = atk[i + 1];
                if (v != sol::nil)
                    crea.mData.mAttack[i] = v.as<int>();
            }
        }
        setCreatureFlag("canFly", ESM::Creature::Flies);
        setCreatureFlag("canSwim", ESM::Creature::Swims);
        setCreatureFlag("canUseWeapons", ESM::Creature::Weapon);
        setCreatureFlag("canWalk", ESM::Creature::Walks);
        setCreatureFlag("isBiped", ESM::Creature::Bipedal);
        setCreatureFlag("isEssential", ESM::Creature::Essential);
        setCreatureFlag("isRespawning", ESM::Creature::Respawn);

        if (rec["isPersistent"] != sol::nil)
        {
            bool persistent = rec["isPersistent"];
            if (persistent)
                crea.mRecordFlags |= ESM::FLAG_Persistent;
            else
                crea.mRecordFlags &= ~ESM::FLAG_Persistent;
        }

        if (rec["bloodType"] != sol::nil)
            crea.mBloodType = rec["bloodType"].get<int>();

        if (rec["servicesOffered"] != sol::nil)
        {
            const sol::table services = rec["servicesOffered"];
            int flags = 0;
            for (const auto& [mask, key] : MWLua::ServiceNames)
            {
                sol::object value = services[key];
                if (value != sol::nil && value.as<bool>())
                    flags |= mask;
            }
            crea.mAiData.mServices = flags;
        }

        return crea;
    }
}

namespace sol
{
    template <>
    struct is_automagical<ESM::Creature> : std::false_type
    {
    };
}

namespace MWLua
{
    namespace
    {
        template <class T>
        void addAttackProperty(sol::usertype<T>& record)
        {
            const auto getter = [](const T& rec, sol::this_state state) -> sol::table {
                sol::table res(state, sol::create);
                int index = 1;
                for (auto attack : Types::RecordType<T>::asRecord(rec).mData.mAttack)
                    res[index++] = attack;
                return LuaUtil::makeReadOnly(res);
            };
            if constexpr (Types::RecordType<T>::isMutable)
            {
                record["attack"] = sol::property(std::move(getter), [](T& rec, const sol::table& values) {
                    auto& attack = rec.find().mData.mAttack;
                    for (int i = 0; i < 6; ++i)
                    {
                        sol::object value = values[i + 1];
                        if (value != sol::nil)
                            attack[i] = value.as<int>();
                    }
                });
            }
            else
            {
                record["attack"] = sol::readonly_property(std::move(getter));
            }
        }

        template <class T>
        void addUserType(sol::state_view& lua, std::string_view name)
        {
            sol::usertype<T> record = lua.new_usertype<T>(name);

            record[sol::meta_function::to_string]
                = [](const T& rec) { return "ESM3_Creature[" + rec.mId.toDebugString() + "]"; };
            record["id"] = sol::readonly_property([](const T& rec) -> ESM::RefId { return rec.mId; });

            Types::addProperty(record, "name", &ESM::Creature::mName);
            Types::addModelProperty(record);
            Types::addProperty(record, "mwscript", &ESM::Creature::mScript);
            Types::addProperty(record, "baseCreature", &ESM::Creature::mOriginal);
            Types::addProperty(record, "soulValue", &ESM::Creature::mData, &ESM::Creature::NPDTstruct::mSoul);
            Types::addProperty(record, "type", &ESM::Creature::mData, &ESM::Creature::NPDTstruct::mType);
            Types::addProperty(record, "baseGold", &ESM::Creature::mData, &ESM::Creature::NPDTstruct::mGold);
            Types::addProperty(record, "combatSkill", &ESM::Creature::mData, &ESM::Creature::NPDTstruct::mCombat);
            Types::addProperty(record, "magicSkill", &ESM::Creature::mData, &ESM::Creature::NPDTstruct::mMagic);
            Types::addProperty(record, "stealthSkill", &ESM::Creature::mData, &ESM::Creature::NPDTstruct::mStealth);
            addAttackProperty(record);
            Types::addFlagProperty(record, "canFly", ESM::Creature::Flies, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "canSwim", ESM::Creature::Swims, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "canUseWeapons", ESM::Creature::Weapon, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "canWalk", ESM::Creature::Walks, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "isBiped", ESM::Creature::Bipedal, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "isEssential", ESM::Creature::Essential, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "isRespawning", ESM::Creature::Respawn, &ESM::Creature::mFlags);
            Types::addFlagProperty(record, "isPersistent", ESM::FLAG_Persistent, &ESM::Creature::mRecordFlags);
            Types::addProperty(record, "bloodType", &ESM::Creature::mBloodType);
        }
    }

    ESM::Creature tableToCreature(const sol::table& rec)
    {
        return tableToCreatureImpl(rec);
    }

    void addMutableCreatureType(sol::state_view& lua)
    {
        addUserType<MutableRecord<ESM::Creature>>(lua, "ESM3_MutableCreature");
    }

    void addCreatureBindings(sol::table creature, const Context& context)
    {
        sol::state_view lua = context.sol();
        creature["TYPE"] = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, int>(lua,
            {
                { "Creatures", ESM::Creature::Creatures },
                { "Daedra", ESM::Creature::Daedra },
                { "Undead", ESM::Creature::Undead },
                { "Humanoid", ESM::Creature::Humanoid },
            }));
        creature["createRecordDraft"] = tableToCreature;

        addRecordFunctionBinding<ESM::Creature>(creature, context);

        sol::usertype<ESM::Creature> record = lua.new_usertype<ESM::Creature>("ESM3_Creature");
        record[sol::meta_function::to_string]
            = [](const ESM::Creature& rec) { return "ESM3_Creature[" + rec.mId.toDebugString() + "]"; };
        record["id"]
            = sol::readonly_property([](const ESM::Creature& rec) -> std::string { return rec.mId.serializeText(); });
        record["name"] = sol::readonly_property([](const ESM::Creature& rec) -> std::string { return rec.mName; });
        addModelProperty(record);
        record["mwscript"] = sol::readonly_property([](const ESM::Creature& rec) -> ESM::RefId { return rec.mScript; });
        record["baseCreature"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> std::string { return rec.mOriginal.serializeText(); });
        record["soulValue"] = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mData.mSoul; });
        record["type"] = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mData.mType; });
        record["baseGold"] = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mData.mGold; });
        record["combatSkill"]
            = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mData.mCombat; });
        record["magicSkill"] = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mData.mMagic; });
        record["stealthSkill"]
            = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mData.mStealth; });
        record["attack"] = sol::readonly_property([lua = lua.lua_state()](const ESM::Creature& rec) -> sol::table {
            sol::table res(lua, sol::create);
            int index = 1;
            for (auto attack : rec.mData.mAttack)
                res[index++] = attack;
            return LuaUtil::makeReadOnly(res);
        });
        record["canFly"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Flies; });
        record["canSwim"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Swims; });
        record["canUseWeapons"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Weapon; });
        record["canWalk"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Walks; });
        record["isBiped"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Bipedal; });
        record["isEssential"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Essential; });
        record["isRespawning"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mFlags & ESM::Creature::Respawn; });
        record["isPersistent"] = sol::readonly_property(
            [](const ESM::Creature& rec) -> bool { return rec.mRecordFlags & ESM::FLAG_Persistent; });
        record["bloodType"] = sol::readonly_property([](const ESM::Creature& rec) -> int { return rec.mBloodType; });

        addActorServicesBindings<ESM::Creature>(record, context);
    }
}
