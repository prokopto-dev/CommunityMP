#include "types.hpp"

#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include "apps/openmw/mwworld/esmstore.hpp"

#include "../luamanagerimp.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "apps/openmw/mwmp/LocalPlayer.hpp"
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/Networking.hpp"
#include "apps/openmw/mwmp/ObjectList.hpp"
#endif

namespace MWLua
{
    namespace
    {
        void applyLock(const GObject& object, int lockLevel)
        {
            object.ptr().getCellRef().setLocked(true);
            object.ptr().getCellRef().setLockLevel(lockLevel);
        }

        void applyUnlock(const GObject& object)
        {
            object.ptr().getCellRef().setLocked(false);
            object.ptr().getCellRef().setLockLevel(-object.ptr().getCellRef().getLockLevel());
        }

#ifdef BUILD_TES3MP_CLIENT
        bool canSendTes3mpLuaObjectPacket(const MWWorld::Ptr& ptr, Context::Type contextType)
        {
            if (contextType != Context::Global || !mwmp::Main::isInitialized() || !ptr.isInCell())
                return false;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            return localPlayer != nullptr && localPlayer->isLoggedIn();
        }

        mwmp::ObjectList* prepareTes3mpLuaObjectPacket()
        {
            mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_SCRIPT_GLOBAL;
            objectList->originClientScript = "openmw-lua:global";
            return objectList;
        }

        bool sendTes3mpLuaObjectLockPacket(const MWWorld::Ptr& ptr, int lockLevel, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectLock(ptr, lockLevel);
            objectList->sendObjectLock();
            return true;
        }

        bool shouldQueueTes3mpLuaObjectPacket(Context::Type contextType)
        {
            return contextType == Context::Global && mwmp::Main::isInitialized();
        }
#endif
    }

    void addLockableBindings(sol::table lockable, const Context& context)
    {
        lockable["getLockLevel"]
            = [](const Object& object) { return std::abs(object.ptr().getCellRef().getLockLevel()); };
        lockable["isLocked"] = [](const Object& object) { return object.ptr().getCellRef().isLocked(); };
        lockable["getKeyRecord"] = [](const Object& object) -> sol::optional<const ESM::Miscellaneous*> {
            ESM::RefId key = object.ptr().getCellRef().getKey();
            if (key.empty())
                return sol::nullopt;
            return MWBase::Environment::get().getESMStore()->get<ESM::Miscellaneous>().find(key);
        };
        lockable["lock"] = [context](const GObject& object, sol::optional<int> lockLevel) {
            int level = 1;

            if (lockLevel)
                level = lockLevel.value();
            else if (object.ptr().getCellRef().getLockLevel() < 0)
                level = -object.ptr().getCellRef().getLockLevel();
            else if (object.ptr().getCellRef().getLockLevel() > 0)
                level = object.ptr().getCellRef().getLockLevel();

#ifdef BUILD_TES3MP_CLIENT
            if (shouldQueueTes3mpLuaObjectPacket(context.mType))
            {
                context.mLuaManager->addAction(
                    [object, level, contextType = context.mType] {
                        if (sendTes3mpLuaObjectLockPacket(object.ptr(), level, contextType))
                            return;
                        applyLock(object, level);
                    },
                    "TES3MP Lua object lock");
                return;
            }
#endif
            applyLock(object, level);
        };
        lockable["unlock"] = [context](const GObject& object) {
            if (!object.ptr().getCellRef().isLocked())
                return;

#ifdef BUILD_TES3MP_CLIENT
            if (shouldQueueTes3mpLuaObjectPacket(context.mType))
            {
                context.mLuaManager->addAction(
                    [object, contextType = context.mType] {
                        if (!object.ptr().getCellRef().isLocked())
                            return;
                        if (sendTes3mpLuaObjectLockPacket(object.ptr(), 0, contextType))
                            return;
                        applyUnlock(object);
                    },
                    "TES3MP Lua object unlock");
                return;
            }
#endif
            applyUnlock(object);
        };
        lockable["setTrapSpell"] = [](const GObject& object, const sol::object& spellOrId) {
            if (spellOrId == sol::nil)
            {
                object.ptr().getCellRef().setTrap(ESM::RefId()); // remove the trap value
                return;
            }
            if (spellOrId.is<ESM::Spell>())
                object.ptr().getCellRef().setTrap(spellOrId.as<const ESM::Spell*>()->mId);
            else
            {
                ESM::RefId spellId = ESM::RefId::deserializeText(LuaUtil::cast<std::string_view>(spellOrId));
                const auto& spellStore = MWBase::Environment::get().getESMStore()->get<ESM::Spell>();
                const ESM::Spell* spell = spellStore.find(spellId);
                object.ptr().getCellRef().setTrap(spell->mId);
            }
        };
        lockable["setKeyRecord"] = [](const GObject& object, const sol::object& itemOrRecordId) {
            if (itemOrRecordId == sol::nil)
            {
                object.ptr().getCellRef().setKey(ESM::RefId()); // remove the trap value
                return;
            }
            if (itemOrRecordId.is<ESM::Miscellaneous>())
                object.ptr().getCellRef().setKey(itemOrRecordId.as<const ESM::Miscellaneous*>()->mId);
            else
            {
                ESM::RefId miscId = ESM::RefId::deserializeText(LuaUtil::cast<std::string_view>(itemOrRecordId));
                const auto& keyStore = MWBase::Environment::get().getESMStore()->get<ESM::Miscellaneous>();
                const ESM::Miscellaneous* key = keyStore.find(miscId);
                object.ptr().getCellRef().setKey(key->mId);
            }
        };
        lockable["getTrapSpell"] = [](sol::this_state lua, const Object& o) -> sol::optional<const ESM::Spell*> {
            ESM::RefId trap = o.ptr().getCellRef().getTrap();
            if (trap.empty())
                return sol::nullopt;
            return MWBase::Environment::get().getESMStore()->get<ESM::Spell>().find(trap);
        };
    }
}
