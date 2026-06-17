#include "types.hpp"

#include "../contentbindings.hpp"
#include "modelproperty.hpp"
#include "usertypeutil.hpp"

#include <components/esm3/loadappa.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/util.hpp>
#include <components/misc/finitevalues.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include "apps/openmw/mwbase/environment.hpp"

namespace sol
{
    template <>
    struct is_automagical<ESM::Apparatus> : std::false_type
    {
    };
}

namespace MWLua
{
    namespace
    {
        template <class T>
        void addUserType(sol::state_view& lua, std::string_view name)
        {
            sol::usertype<T> record = lua.new_usertype<T>(name);

            record[sol::meta_function::to_string]
                = [](const T& rec) -> std::string { return "ESM3_Apparatus[" + rec.mId.toDebugString() + "]"; };
            record["id"] = sol::readonly_property([](const T& rec) -> ESM::RefId { return rec.mId; });

            Types::addProperty(record, "name", &ESM::Apparatus::mName);
            Types::addModelProperty(record);
            Types::addProperty(record, "mwscript", &ESM::Apparatus::mScript);
            Types::addIconProperty(record);
            Types::addProperty(record, "type", &ESM::Apparatus::mData, &ESM::Apparatus::AADTstruct::mType);
            Types::addProperty(record, "value", &ESM::Apparatus::mData, &ESM::Apparatus::AADTstruct::mValue);
            Types::addProperty(record, "weight", &ESM::Apparatus::mData, &ESM::Apparatus::AADTstruct::mWeight);
            Types::addProperty(record, "quality", &ESM::Apparatus::mData, &ESM::Apparatus::AADTstruct::mQuality);
        }
    }

    ESM::Apparatus tableToApparatus(const sol::table& rec)
    {
        auto apparatus = Types::initFromTemplate<ESM::Apparatus>(rec);

        if (rec["name"] != sol::nil)
            apparatus.mName = rec["name"];
        if (rec["model"] != sol::nil)
            apparatus.mModel = Misc::ResourceHelpers::meshPathForESM3(rec["model"].get<std::string_view>());
        if (rec["icon"] != sol::nil)
            apparatus.mIcon = rec["icon"];
        if (rec["mwscript"] != sol::nil)
            apparatus.mScript = ESM::RefId::deserializeText(rec["mwscript"].get<std::string_view>());
        if (rec["type"] != sol::nil)
        {
            int apparatusType = rec["type"].get<int>();
            if (apparatusType >= ESM::Apparatus::MortarPestle && apparatusType <= ESM::Apparatus::Retort)
                apparatus.mData.mType = apparatusType;
            else
                throw std::runtime_error("Invalid Apparatus Type provided: " + std::to_string(apparatusType));
        }
        if (rec["value"] != sol::nil)
            apparatus.mData.mValue = rec["value"];
        if (rec["weight"] != sol::nil)
            apparatus.mData.mWeight = rec["weight"].get<Misc::FiniteFloat>();
        if (rec["quality"] != sol::nil)
            apparatus.mData.mQuality = rec["quality"].get<Misc::FiniteFloat>();

        return apparatus;
    }

    void addMutableApparatusType(sol::state_view& lua)
    {
        addUserType<MutableRecord<ESM::Apparatus>>(lua, "ESM3_MutableApparatus");
    }

    void addApparatusBindings(sol::table apparatus, const Context& context)
    {
        sol::state_view lua = context.sol();
        apparatus["TYPE"] = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, int>(lua,
            {
                { "MortarPestle", ESM::Apparatus::MortarPestle },
                { "Alembic", ESM::Apparatus::Alembic },
                { "Calcinator", ESM::Apparatus::Calcinator },
                { "Retort", ESM::Apparatus::Retort },
            }));

        auto vfs = MWBase::Environment::get().getResourceSystem()->getVFS();

        addRecordFunctionBinding<ESM::Apparatus>(apparatus, context);
        apparatus["createRecordDraft"] = tableToApparatus;

        sol::usertype<ESM::Apparatus> record = lua.new_usertype<ESM::Apparatus>("ESM3_Apparatus");
        record[sol::meta_function::to_string]
            = [](const ESM::Apparatus& rec) { return "ESM3_Apparatus[" + rec.mId.toDebugString() + "]"; };
        record["id"]
            = sol::readonly_property([](const ESM::Apparatus& rec) -> std::string { return rec.mId.serializeText(); });
        record["name"] = sol::readonly_property([](const ESM::Apparatus& rec) -> std::string { return rec.mName; });
        addModelProperty(record);
        record["mwscript"]
            = sol::readonly_property([](const ESM::Apparatus& rec) -> ESM::RefId { return rec.mScript; });
        record["icon"] = sol::readonly_property([vfs](const ESM::Apparatus& rec) -> std::string {
            return Misc::ResourceHelpers::correctIconPath(VFS::Path::toNormalized(rec.mIcon), *vfs);
        });
        record["type"] = sol::readonly_property([](const ESM::Apparatus& rec) -> int { return rec.mData.mType; });
        record["value"] = sol::readonly_property([](const ESM::Apparatus& rec) -> int { return rec.mData.mValue; });
        record["weight"] = sol::readonly_property([](const ESM::Apparatus& rec) -> float { return rec.mData.mWeight; });
        record["quality"]
            = sol::readonly_property([](const ESM::Apparatus& rec) -> float { return rec.mData.mQuality; });
    }
}
