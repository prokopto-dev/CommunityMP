#include "objectbindings.hpp"

#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/shapes/box.hpp>
#include <components/lua/util.hpp>
#include <components/lua/utilpackage.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/mathutil.hpp>

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/localscripts.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwrender/renderingmanager.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#ifdef BUILD_TES3MP_CLIENT
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/PlayerList.hpp"
#endif

#include "luaevents.hpp"
#include "luamanagerimp.hpp"
#include "types/types.hpp"

namespace sol
{
    template <>
    struct is_automagical<MWLua::LObject> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::GObject> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::LObjectList> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::GObjectList> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::Inventory<MWLua::LObject>> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::Inventory<MWLua::GObject>> : std::false_type
    {
    };
}

namespace MWLua
{

    namespace
    {
        MWWorld::CellStore* findCell(const sol::object& cellOrName, const osg::Vec3f& pos)
        {
            MWWorld::WorldModel* wm = MWBase::Environment::get().getWorldModel();
            MWWorld::CellStore* cell;
            if (cellOrName.is<GCell>())
                cell = cellOrName.as<const GCell&>().mStore;
            else
            {
                std::string_view name = LuaUtil::cast<std::string_view>(cellOrName);
                if (name.empty())
                    cell = nullptr; // default exterior worldspace
                else
                    cell = &wm->getCell(name);
            }
            if (cell != nullptr && !cell->isExterior())
                return cell;
            const ESM::RefId worldspace
                = cell == nullptr ? ESM::Cell::sDefaultWorldspaceId : cell->getCell()->getWorldSpace();
            return &wm->getExterior(ESM::positionToExteriorCellLocation(pos.x(), pos.y(), worldspace));
        }

        ESM::Position toPos(const osg::Vec3f& pos, const osg::Vec3f& rot)
        {
            ESM::Position esmPos;
            static_assert(sizeof(esmPos) == sizeof(osg::Vec3f) * 2);
            std::memcpy(esmPos.pos, &pos, sizeof(osg::Vec3f));
            std::memcpy(esmPos.rot, &rot, sizeof(osg::Vec3f));
            return esmPos;
        }

#ifdef BUILD_TES3MP_CLIENT
        ESM::Cell makeTes3mpLuaPacketCell(const MWWorld::Cell& cell)
        {
            ESM::Cell packetCell;

            if (cell.isExterior())
            {
                packetCell.mData.mX = cell.getGridX();
                packetCell.mData.mY = cell.getGridY();
            }
            else
            {
                packetCell.mData.mFlags = ESM::Cell::Interior;
                packetCell.mName = std::string(cell.getNameId());
            }

            packetCell.mRegion = cell.getRegion();
            packetCell.updateId();
            return packetCell;
        }

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

        bool sendTes3mpLuaObjectStatePacket(const MWWorld::Ptr& ptr, bool enabled, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectState(ptr, enabled);
            objectList->sendObjectState();
            return true;
        }

        bool sendTes3mpLuaObjectScalePacket(const MWWorld::Ptr& ptr, float scale, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectScale(ptr, scale);
            objectList->sendObjectScale();
            return true;
        }

        bool sendTes3mpLuaObjectDeletePacket(const MWWorld::Ptr& ptr, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectGeneric(ptr);
            objectList->sendObjectDelete();
            return true;
        }

        bool sendTes3mpLuaContainerAddPacket(
            const MWWorld::Ptr& container, const MWWorld::Ptr& item, int itemCount, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(container, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->cell = makeTes3mpLuaPacketCell(*container.getCell()->getCell());
            objectList->action = mwmp::BaseObjectList::ADD;
            objectList->containerSubAction = mwmp::BaseObjectList::NONE;

            mwmp::BaseObject baseObject = objectList->getBaseObjectFromPtr(container);
            objectList->addContainerItem(baseObject, item, itemCount, 0);
            objectList->addBaseObject(baseObject);
            objectList->sendContainer();
            return true;
        }

        bool sendTes3mpLuaContainerRemovePacket(
            const MWWorld::Ptr& container, const MWWorld::Ptr& item, int itemCount, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(container, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->cell = makeTes3mpLuaPacketCell(*container.getCell()->getCell());
            objectList->action = mwmp::BaseObjectList::REMOVE;
            objectList->containerSubAction = mwmp::BaseObjectList::NONE;

            mwmp::BaseObject baseObject = objectList->getBaseObjectFromPtr(container);
            objectList->addContainerItem(baseObject, item, itemCount, itemCount);
            objectList->addBaseObject(baseObject);
            objectList->sendContainer();
            return true;
        }

        bool sendTes3mpLuaLocalPlayerInventoryPacket(
            const MWWorld::Ptr& container, const MWWorld::Ptr& item, int itemCount, unsigned int action,
            Context::Type contextType)
        {
            if (contextType != Context::Global || !mwmp::Main::isInitialized())
                return false;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            if (localPlayer == nullptr || !localPlayer->isLoggedIn()
                || container != MWBase::Environment::get().getWorld()->getPlayerPtr())
                return false;

            std::string itemRefId = item.getCellRef().getRefId().serializeText();
            if (itemRefId.find("$dynamic") != std::string::npos)
                return false;

            localPlayer->sendItemChange(item, itemCount, action);
            return true;
        }

        bool sendTes3mpLuaObjectRemovePacket(const MWWorld::Ptr& ptr, int itemCount, Context::Type contextType)
        {
            if (!ptr.getContainerStore())
                return false;

            MWWorld::Ptr sourceContainerPtr = ptr.getContainerStore()->getPtr();
            if (sourceContainerPtr.isEmpty())
                return false;

            MWBase::World* world = MWBase::Environment::get().getWorld();
            if (sourceContainerPtr == world->getPlayerPtr())
                return sendTes3mpLuaLocalPlayerInventoryPacket(
                    sourceContainerPtr, ptr, itemCount, mwmp::InventoryChanges::REMOVE, contextType);

            if (sourceContainerPtr.isInCell() && !mwmp::PlayerList::isDedicatedPlayer(sourceContainerPtr))
                return sendTes3mpLuaContainerRemovePacket(sourceContainerPtr, ptr, itemCount, contextType);

            return false;
        }

        bool sendTes3mpLuaObjectPlacePacket(const MWWorld::Ptr& ptr, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType))
                return false;

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectPlace(ptr);
            if (objectList->baseObjects.empty())
                return false;

            objectList->sendObjectPlace();
            return true;
        }

        bool sendTes3mpLuaObjectTeleportPacket(const MWWorld::Ptr& ptr, MWWorld::CellStore* destCell,
            const osg::Vec3f& pos, const osg::Vec3f& rot, bool placeOnGround, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType) || ptr.getCell() != destCell || placeOnGround)
                return false;

            ESM::Position position = toPos(pos, rot);

            mwmp::ObjectList* objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectMove(ptr, position);
            objectList->sendObjectMove();

            objectList = prepareTes3mpLuaObjectPacket();
            objectList->addObjectRotate(ptr, position);
            objectList->sendObjectRotate();
            return true;
        }

        bool canSendTes3mpLuaObjectCrossCellTeleportPacket(
            const MWWorld::Ptr& ptr, MWWorld::CellStore* destCell, Context::Type contextType)
        {
            if (!canSendTes3mpLuaObjectPacket(ptr, contextType) || ptr.getCell() == destCell || ptr.getClass().isDoor()
                || ptr.getClass().isActor())
                return false;

            return ptr.getCell() != &MWBase::Environment::get().getWorldModel()->getDraftCell();
        }

        bool canSendTes3mpLuaObjectGroundedSameCellTeleportPacket(
            const MWWorld::Ptr& ptr, MWWorld::CellStore* destCell, bool placeOnGround, Context::Type contextType)
        {
            if (!placeOnGround || !canSendTes3mpLuaObjectPacket(ptr, contextType) || ptr.getCell() != destCell
                || ptr.getClass().isActor())
                return false;

            return ptr.getCell() != &MWBase::Environment::get().getWorldModel()->getDraftCell();
        }
#endif

#ifdef BUILD_TES3MP_CLIENT
        void queueTes3mpScriptCellChangeReasonForPlayer()
        {
            if (!mwmp::Main::isInitialized())
                return;

            if (mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer())
                localPlayer->queueCellChangeReason(mwmp::CELL_CHANGE_REASON_SCRIPT);
        }
#endif

        void teleportPlayer(
            MWWorld::CellStore* destCell, const osg::Vec3f& pos, const osg::Vec3f& rot, bool placeOnGround)
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWWorld::Ptr ptr = world->getPlayerPtr();
            auto& stats = ptr.getClass().getCreatureStats(ptr);
            stats.land(true);
            stats.setTeleported(true);
            world->getPlayer().setTeleported(true);
            bool differentCell = ptr.getCell() != destCell;
#ifdef BUILD_TES3MP_CLIENT
            if (differentCell)
                queueTes3mpScriptCellChangeReasonForPlayer();
#endif
            world->changeToCell(destCell->getCell()->getId(), toPos(pos, rot), false, differentCell);
            MWWorld::Ptr newPtr = world->getPlayerPtr();
            world->moveObject(newPtr, pos);
            world->rotateObject(newPtr, rot);
            if (placeOnGround)
                world->adjustPosition(newPtr, true);
            MWBase::Environment::get().getLuaManager()->objectTeleported(newPtr);
        }

        MWWorld::Ptr teleportNotPlayer(const MWWorld::Ptr& ptr, MWWorld::CellStore* destCell, const osg::Vec3f& pos,
            const osg::Vec3f& rot, bool placeOnGround)
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWWorld::WorldModel* wm = MWBase::Environment::get().getWorldModel();
            const MWWorld::Class& cls = ptr.getClass();
            if (cls.isActor())
            {
                auto& stats = cls.getCreatureStats(ptr);
                stats.land(false);
                stats.setTeleported(true);
            }
            const MWWorld::CellStore* srcCell = ptr.getCell();
            MWWorld::Ptr newPtr;
            if (srcCell == &wm->getDraftCell())
            {
                newPtr = cls.moveToCell(ptr, *destCell, toPos(pos, rot));
                ptr.getCellRef().unsetRefNum();
                ptr.getRefData().setLuaScripts(nullptr);
                ptr.getCellRef().setCount(0);
                ESM::RefId script = cls.getScript(newPtr);
                if (!script.empty())
                    world->getLocalScripts().add(script, newPtr);
                world->addContainerScripts(newPtr, newPtr.getCell());
            }
            else
            {
                newPtr = world->moveObject(ptr, destCell, pos);
                if (srcCell == destCell)
                {
                    ESM::RefId script = cls.getScript(newPtr);
                    if (!script.empty())
                        world->getLocalScripts().add(script, newPtr);
                }
                world->rotateObject(newPtr, rot, MWBase::RotationFlag_none);
            }
            if (placeOnGround)
                world->adjustPosition(newPtr, true);
            if (cls.isDoor())
            { // Change "original position and rotation" because without it teleported animated doors don't work
              // properly.
                newPtr.getCellRef().setPosition(newPtr.getRefData().getPosition());
            }
            if (!newPtr.getRefData().isEnabled())
                world->enable(newPtr);
            MWBase::Environment::get().getLuaManager()->objectTeleported(newPtr);
            return newPtr;
        }

        template <typename ObjT>
        using Cell = std::conditional_t<std::is_same_v<ObjT, LObject>, LCell, GCell>;

        template <class ObjectT>
        void registerObjectList(const std::string& prefix, const Context& context)
        {
            using ListT = ObjectList<ObjectT>;
            sol::state_view lua = context.sol();
            sol::usertype<ListT> listT = lua.new_usertype<ListT>(prefix + "ObjectList");
            listT[sol::meta_function::to_string]
                = [](const ListT& list) { return "{" + std::to_string(list.mIds->size()) + " objects}"; };
            listT[sol::meta_function::length] = [](const ListT& list) { return list.mIds->size(); };
            listT[sol::meta_function::index] = [](const ListT& list, size_t index) -> sol::optional<ObjectT> {
                if (index > 0 && index <= list.mIds->size())
                    return ObjectT((*list.mIds)[LuaUtil::fromLuaIndex(index)]);
                else
                    return sol::nullopt;
            };
            listT[sol::meta_function::pairs] = lua["ipairsForArray"].template get<sol::function>();
            listT[sol::meta_function::ipairs] = lua["ipairsForArray"].template get<sol::function>();
        }

        osg::Vec3f toEulerRotation(const sol::object& transform, bool isActor)
        {
            if (transform.is<LuaUtil::TransformQ>())
            {
                const osg::Quat& q = transform.as<LuaUtil::TransformQ>().mQ;
                return isActor ? Misc::toEulerAnglesXZ(q) : Misc::toEulerAnglesZYX(q);
            }
            else
            {
                const osg::Matrixf& m = LuaUtil::cast<LuaUtil::TransformM>(transform).mM;
                return isActor ? Misc::toEulerAnglesXZ(m) : Misc::toEulerAnglesZYX(m);
            }
        }

        osg::Quat toQuat(const ESM::Position& pos, bool isActor)
        {
            if (isActor)
                return osg::Quat(pos.rot[0], osg::Vec3(-1, 0, 0)) * osg::Quat(pos.rot[2], osg::Vec3(0, 0, -1));
            else
                return Misc::Convert::makeOsgQuat(pos.rot);
        }

        template <class ObjectT>
        void addOwnerbindings(sol::usertype<ObjectT>& objectT, const std::string& prefix, const Context& context)
        {
            using OwnerT = Owner<ObjectT>;
            sol::usertype<OwnerT> ownerT = context.sol().new_usertype<OwnerT>(prefix + "Owner");

            ownerT[sol::meta_function::to_string] = [](const OwnerT& o) { return "Owner[" + o.mObj.toString() + "]"; };

            auto getOwnerRecordId = [](const OwnerT& o) -> sol::optional<std::string> {
                ESM::RefId owner = o.mObj.ptr().getCellRef().getOwner();
                if (owner.empty())
                    return sol::nullopt;
                else
                    return owner.serializeText();
            };
            auto setOwnerRecordId = [](const OwnerT& o, sol::optional<std::string_view> ownerId) {
                if (std::is_same_v<ObjectT, LObject> && !(o.mObj.isSelfObject()))
                    throw std::runtime_error("Local scripts can set an owner only on self");
                const MWWorld::Ptr& ptr = o.mObj.ptr();

                if (!ownerId)
                {
                    ptr.getCellRef().setOwner(ESM::RefId());
                    return;
                }
                ESM::RefId owner = ESM::RefId::deserializeText(*ownerId);
                const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
                if (!store.get<ESM::NPC>().search(owner))
                    throw std::runtime_error("Invalid owner record id");
                ptr.getCellRef().setOwner(owner);
            };
            ownerT["recordId"] = sol::property(getOwnerRecordId, setOwnerRecordId);

            auto getOwnerFactionId = [](const OwnerT& o) -> sol::optional<std::string> {
                ESM::RefId owner = o.mObj.ptr().getCellRef().getFaction();
                if (owner.empty())
                    return sol::nullopt;
                else
                    return owner.serializeText();
            };
            auto setOwnerFactionId = [](const OwnerT& o, sol::optional<std::string> ownerId) {
                ESM::RefId ownerFac;
                if (std::is_same_v<ObjectT, LObject> && !(o.mObj.isSelfObject()))
                    throw std::runtime_error("Local scripts can set an owner faction only on self");
                if (!ownerId)
                {
                    o.mObj.ptr().getCellRef().setFaction(ESM::RefId());
                    return;
                }
                ownerFac = ESM::RefId::deserializeText(*ownerId);
                const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
                if (!store.get<ESM::Faction>().search(ownerFac))
                    throw std::runtime_error("Invalid owner faction id");
                o.mObj.ptr().getCellRef().setFaction(ownerFac);
            };
            ownerT["factionId"] = sol::property(getOwnerFactionId, setOwnerFactionId);

            auto getOwnerFactionRank = [](const OwnerT& o) -> sol::optional<int64_t> {
                int rank = o.mObj.ptr().getCellRef().getFactionRank();
                if (rank < 0)
                    return sol::nullopt;
                return LuaUtil::toLuaIndex(rank);
            };
            auto setOwnerFactionRank = [](const OwnerT& o, sol::optional<int64_t> factionRank) {
                if (std::is_same_v<ObjectT, LObject> && !(o.mObj.isSelfObject()))
                    throw std::runtime_error("Local scripts can set an owner faction rank only on self");
                int64_t rank = std::max<int64_t>(0, LuaUtil::fromLuaIndex(factionRank.value_or(0)));
                o.mObj.ptr().getCellRef().setFactionRank(static_cast<int>(rank));
            };
            ownerT["factionRank"] = sol::property(getOwnerFactionRank, setOwnerFactionRank);

            objectT["owner"] = sol::readonly_property([](const ObjectT& object) { return OwnerT{ object }; });
        }

        template <class ObjectT>
        void addBasicBindings(sol::usertype<ObjectT>& objectT, const Context& context)
        {
            objectT["id"] = sol::readonly_property([](const ObjectT& o) -> std::string { return o.id().toString(); });
            objectT["contentFile"] = sol::readonly_property([](const ObjectT& o) -> sol::optional<std::string> {
                int contentFileIndex = o.id().mContentFile;
                const std::vector<std::string>& contentList = MWBase::Environment::get().getWorld()->getContentFiles();
                if (contentFileIndex < 0 || contentFileIndex >= static_cast<int>(contentList.size()))
                    return sol::nullopt;
                return Misc::StringUtils::lowerCase(contentList[contentFileIndex]);
            });
            objectT["isValid"] = [](const ObjectT& o) { return !o.ptrOrEmpty().isEmpty(); };
            objectT["recordId"] = sol::readonly_property(
                [](const ObjectT& o) -> std::string { return o.ptr().getCellRef().getRefId().serializeText(); });
            objectT["globalVariable"] = sol::readonly_property([](const ObjectT& o) -> sol::optional<std::string> {
                std::string_view globalVariable = o.ptr().getCellRef().getGlobalVariable();
                if (globalVariable.empty())
                    return sol::nullopt;
                else
                    return ESM::RefId::stringRefId(globalVariable).serializeText();
            });
            objectT["cell"] = sol::readonly_property([](const ObjectT& o) -> sol::optional<Cell<ObjectT>> {
                const MWWorld::Ptr& ptr = o.ptr();
                MWWorld::WorldModel* wm = MWBase::Environment::get().getWorldModel();
                if (ptr.isInCell() && ptr.getCell() != &wm->getDraftCell())
                    return Cell<ObjectT>{ ptr.getCell() };
                else
                    return sol::nullopt;
            });
            objectT["parentContainer"] = sol::readonly_property([](const ObjectT& o) -> sol::optional<ObjectT> {
                const MWWorld::Ptr& ptr = o.ptr();
                if (ptr.getContainerStore())
                    return ObjectT(ptr.getContainerStore()->getPtr());
                else
                    return sol::nullopt;
            });
            objectT["position"] = sol::readonly_property(
                [](const ObjectT& o) -> osg::Vec3f { return o.ptr().getRefData().getPosition().asVec3(); });
            objectT["scale"]
                = sol::readonly_property([](const ObjectT& o) -> float { return o.ptr().getCellRef().getScale(); });
            objectT["rotation"] = sol::readonly_property([](const ObjectT& o) -> LuaUtil::TransformQ {
                return { toQuat(o.ptr().getRefData().getPosition(), o.ptr().getClass().isActor()) };
            });
            objectT["startingCell"] = sol::readonly_property([](const ObjectT& o) -> sol::optional<Cell<ObjectT>> {
                const MWWorld::Ptr& ptr = o.ptr();
                MWWorld::WorldModel* wm = MWBase::Environment::get().getWorldModel();
                if (ptr.isInCell() && ptr.getCell() != &wm->getDraftCell())
                    return Cell<ObjectT>{ ptr.getCell()->getOriginCell(ptr) };
                return sol::nullopt;
            });
            objectT["startingPosition"] = sol::readonly_property(
                [](const ObjectT& o) -> osg::Vec3f { return o.ptr().getCellRef().getPosition().asVec3(); });
            objectT["startingRotation"] = sol::readonly_property([](const ObjectT& o) -> LuaUtil::TransformQ {
                return { toQuat(o.ptr().getCellRef().getPosition(), o.ptr().getClass().isActor()) };
            });
            objectT["getBoundingBox"] = [](const ObjectT& o) {
                MWRender::RenderingManager* renderingManager
                    = MWBase::Environment::get().getWorld()->getRenderingManager();
                osg::BoundingBox bb = renderingManager->getCullSafeBoundingBox(o.ptr());
                return LuaUtil::Box{ bb.center(), bb._max - bb.center() };
            };

            objectT["type"] = sol::readonly_property(
                [types = getTypeToPackageTable(context.sol())](
                    const ObjectT& o) -> sol::object { return types[getLiveCellRefType(o.ptr().mRef)]; });

            objectT["count"] = sol::readonly_property([](const ObjectT& o) { return o.ptr().getCellRef().getCount(); });
            objectT[sol::meta_function::equal_to] = [](const ObjectT& a, const ObjectT& b) { return a.id() == b.id(); };
            objectT[sol::meta_function::to_string] = &ObjectT::toString;
            objectT["sendEvent"] = [context](const ObjectT& dest, std::string eventName, const sol::object& eventData) {
                context.mLuaEvents->addLocalEvent(
                    { dest.id(), std::move(eventName), LuaUtil::serialize(eventData, context.mSerializer) });
            };

            objectT["activateBy"] = [](const ObjectT& object, const ObjectT& actor) {
                const MWWorld::Ptr& objPtr = object.ptr();
                const MWWorld::Ptr& actorPtr = actor.ptr();
                uint32_t esmRecordType = actorPtr.getType();
                if (esmRecordType != ESM::REC_CREA && esmRecordType != ESM::REC_NPC_)
                    throw std::runtime_error(
                        "The argument of `activateBy` must be an actor who activates the object. Got: "
                        + actor.toString());

                MWBase::Environment::get().getLuaManager()->objectActivated(objPtr, actorPtr);
            };

            auto isEnabled = [](const ObjectT& o) { return o.ptr().getRefData().isEnabled(); };
            auto setEnabled = [context](const GObject& object, bool enable) {
                if (enable && object.ptr().mRef->isDeleted())
                    throw std::runtime_error("Object is removed");
                context.mLuaManager->addAction([object, enable, contextType = context.mType] {
                    if (object.ptr().mRef->isDeleted())
                        return;
                    if (object.ptr().isInCell())
                    {
#ifdef BUILD_TES3MP_CLIENT
                        if (sendTes3mpLuaObjectStatePacket(object.ptr(), enable, contextType))
                            return;
#endif
                        if (enable)
                            MWBase::Environment::get().getWorld()->enable(object.ptr());
                        else
                            MWBase::Environment::get().getWorld()->disable(object.ptr());
                    }
                    else
                    {
                        if (enable)
                            object.ptr().getRefData().enable();
                        else
                            throw std::runtime_error("Objects in containers can't be disabled");
                    }
                });
            };
            if constexpr (std::is_same_v<ObjectT, GObject>)
                objectT["enabled"] = sol::property(isEnabled, setEnabled);
            else
                objectT["enabled"] = sol::readonly_property(isEnabled);

            if constexpr (std::is_same_v<ObjectT, GObject>)
            { // Only for global scripts
                objectT["setScale"] = [context](const GObject& object, float scale) {
                    context.mLuaManager->addAction([object, scale, contextType = context.mType] {
#ifdef BUILD_TES3MP_CLIENT
                        if (sendTes3mpLuaObjectScalePacket(object.ptr(), scale, contextType))
                            return;
#endif
                        MWBase::Environment::get().getWorld()->scaleObject(object.ptr(), scale);
                    });
                };
                objectT["addScript"] = [context](const GObject& object, std::string_view path, sol::object initData) {
                    const LuaUtil::ScriptsConfiguration& cfg = context.mLua->getConfiguration();
                    std::optional<int> scriptId = cfg.findId(VFS::Path::Normalized(path));
                    if (!scriptId)
                        throw std::runtime_error("Unknown script: " + std::string(path));
                    if (!(cfg[*scriptId].mFlags & ESM::LuaScriptCfg::sCustom))
                        throw std::runtime_error(
                            "Script without CUSTOM tag can not be added dynamically: " + std::string(path));
                    if (object.ptr().getType() == ESM::REC_STAT)
                        throw std::runtime_error("Attaching scripts to Static is not allowed: " + std::string(path));
                    if (initData != sol::nil)
                        context.mLuaManager->addCustomLocalScript(object.ptr(), *scriptId,
                            LuaUtil::serialize(LuaUtil::cast<sol::table>(initData), context.mSerializer));
                    else
                        context.mLuaManager->addCustomLocalScript(
                            object.ptr(), *scriptId, cfg[*scriptId].mInitializationData);
                };
                objectT["hasScript"] = [lua = context.mLua](const GObject& object, std::string_view path) {
                    const LuaUtil::ScriptsConfiguration& cfg = lua->getConfiguration();
                    std::optional<int> scriptId = cfg.findId(VFS::Path::Normalized(path));
                    if (!scriptId)
                        return false;
                    MWWorld::Ptr ptr = object.ptr();
                    LocalScripts* localScripts = ptr.getRefData().getLuaScripts();
                    if (localScripts)
                        return localScripts->hasScript(*scriptId);
                    else
                        return false;
                };
                objectT["removeScript"] = [lua = context.mLua](const GObject& object, std::string_view path) {
                    const LuaUtil::ScriptsConfiguration& cfg = lua->getConfiguration();
                    std::optional<int> scriptId = cfg.findId(VFS::Path::Normalized(path));
                    if (!scriptId)
                        throw std::runtime_error("Unknown script: " + std::string(path));
                    MWWorld::Ptr ptr = object.ptr();
                    LocalScripts* localScripts = ptr.getRefData().getLuaScripts();
                    if (!localScripts || !localScripts->hasScript(*scriptId))
                        throw std::runtime_error("There is no script " + std::string(path) + " on " + ptr.toString());
                    if (localScripts->getAutoStartConf().count(*scriptId) > 0)
                        throw std::runtime_error("Autostarted script can not be removed: " + std::string(path));
                    localScripts->removeScript(*scriptId);
                };

                using DelayedRemovalFn = std::function<void(MWWorld::Ptr)>;
                auto removeFn = [](const MWWorld::Ptr ptr, int countToRemove) -> std::optional<DelayedRemovalFn> {
                    int rawCount = ptr.getCellRef().getCount(false);
                    int currentCount = std::abs(rawCount);
                    int signedCountToRemove = (rawCount < 0 ? -1 : 1) * countToRemove;

                    if (countToRemove <= 0 || countToRemove > currentCount)
                        throw std::runtime_error("Can't remove " + std::to_string(countToRemove) + " of "
                            + std::to_string(currentCount) + " items");
                    ptr.getCellRef().setCount(rawCount - signedCountToRemove); // Immediately change count
                    if (!ptr.getContainerStore() && currentCount > countToRemove)
                        return std::nullopt;
                    // Delayed action to trigger side effects
                    return [signedCountToRemove](MWWorld::Ptr p) {
                        // Restore the original count
                        p.getCellRef().setCount(p.getCellRef().getCount(false) + signedCountToRemove);
                        // And now remove properly
                        if (p.getContainerStore())
                            p.getContainerStore()->remove(p, std::abs(signedCountToRemove), false);
                        else
                        {
                            MWBase::Environment::get().getWorld()->disable(p);
                            MWBase::Environment::get().getWorld()->deleteObject(p);
                        }
                    };
                };
                objectT["remove"] = [removeFn, context](const GObject& object, sol::optional<int> count) {
#ifdef BUILD_TES3MP_CLIENT
                    MWWorld::Ptr ptr = object.ptr();
                    const int currentCount = std::abs(ptr.getCellRef().getCount(false));
                    const int countToRemove = count.value_or(currentCount);

                    if (context.mType == Context::Global && ptr.getContainerStore())
                    {
                        if (countToRemove <= 0 || countToRemove > currentCount)
                            throw std::runtime_error("Can't remove " + std::to_string(countToRemove) + " of "
                                + std::to_string(currentCount) + " items");

                        context.mLuaManager->addAction(
                            [object, countToRemove, removeFn, contextType = context.mType] {
                                MWWorld::Ptr actionPtr = object.ptr();
                                if (sendTes3mpLuaObjectRemovePacket(actionPtr, countToRemove, contextType))
                                    return;

                                std::optional<DelayedRemovalFn> delayed = removeFn(actionPtr, countToRemove);
                                if (delayed.has_value())
                                    (*delayed)(actionPtr);
                            },
                            "TES3MP Lua object remove");
                        return;
                    }

                    if (context.mType == Context::Global && ptr.isInCell() && !ptr.getContainerStore()
                        && countToRemove == currentCount)
                    {
                        if (countToRemove <= 0)
                            throw std::runtime_error("Can't remove " + std::to_string(countToRemove) + " of "
                                + std::to_string(currentCount) + " items");

                        context.mLuaManager->addAction([object, contextType = context.mType] {
                            MWWorld::Ptr actionPtr = object.ptr();
                            if (sendTes3mpLuaObjectDeletePacket(actionPtr, contextType))
                                return;

                            MWBase::Environment::get().getWorld()->disable(actionPtr);
                            MWBase::Environment::get().getWorld()->deleteObject(actionPtr);
                        });
                        return;
                    }
#endif
                    std::optional<DelayedRemovalFn> delayed
                        = removeFn(object.ptr(), count.value_or(object.ptr().getCellRef().getCount()));
                    if (delayed.has_value())
                        context.mLuaManager->addAction([fn = *delayed, object] { fn(object.ptr()); });
                };
                objectT["split"] = [removeFn, context](const GObject& object, int count) -> GObject {
                    MWWorld::CellStore* cell = &MWBase::Environment::get().getWorldModel()->getDraftCell();

                    const MWWorld::Ptr& ptr = object.ptr();
#ifdef BUILD_TES3MP_CLIENT
                    if (context.mType == Context::Global && ptr.getContainerStore())
                    {
                        const int currentCount = std::abs(ptr.getCellRef().getCount(false));
                        if (count <= 0 || count > currentCount)
                            throw std::runtime_error("Can't remove " + std::to_string(count) + " of "
                                + std::to_string(currentCount) + " items");

                        MWWorld::Ptr splitted = ptr.getClass().copyToCell(ptr, *cell, count);
                        splitted.getRefData().disable();

                        context.mLuaManager->addAction(
                            [object, count, removeFn, contextType = context.mType] {
                                MWWorld::Ptr actionPtr = object.ptr();
                                if (sendTes3mpLuaObjectRemovePacket(actionPtr, count, contextType))
                                    return;

                                std::optional<DelayedRemovalFn> delayed = removeFn(actionPtr, count);
                                if (delayed.has_value())
                                    (*delayed)(actionPtr);
                            },
                            "TES3MP Lua object split");

                        return GObject(splitted);
                    }
#endif
                    MWWorld::Ptr splitted = ptr.getClass().copyToCell(ptr, *cell, count);
                    splitted.getRefData().disable();

                    std::optional<DelayedRemovalFn> delayedRemovalFn = removeFn(ptr, count);
                    if (delayedRemovalFn.has_value())
                        context.mLuaManager->addAction([fn = *delayedRemovalFn, object] { fn(object.ptr()); });

                    return GObject(splitted);
                };
                objectT["moveInto"] = [removeFn, context](const GObject& object, const sol::object& dest) {
                    const MWWorld::Ptr& ptr = object.ptr();
                    int count = ptr.getCellRef().getCount();
                    MWWorld::Ptr destPtr;
                    if (dest.is<GObject>())
                        destPtr = dest.as<GObject>().ptr();
                    else
                        destPtr = LuaUtil::cast<Inventory<GObject>>(dest).mObj.ptr();
                    destPtr.getClass().getContainerStore(destPtr); // raises an error if there is no container store

#ifdef BUILD_TES3MP_CLIENT
                    MWWorld::Ptr sourceContainerPtr;
                    if (ptr.getContainerStore())
                        sourceContainerPtr = ptr.getContainerStore()->getPtr();
                    MWBase::World* tes3mpWorld = MWBase::Environment::get().getWorld();
                    const bool sourceContainerIsPlayer = !sourceContainerPtr.isEmpty()
                        && (sourceContainerPtr == tes3mpWorld->getPlayerPtr()
                            || mwmp::PlayerList::isDedicatedPlayer(sourceContainerPtr));
                    const bool destContainerIsPlayer
                        = destPtr == tes3mpWorld->getPlayerPtr() || mwmp::PlayerList::isDedicatedPlayer(destPtr);
                    const bool sourceContainerIsLocalPlayer
                        = !sourceContainerPtr.isEmpty() && sourceContainerPtr == tes3mpWorld->getPlayerPtr();
                    const bool destContainerIsLocalPlayer = destPtr == tes3mpWorld->getPlayerPtr();
                    const bool syncTes3mpWorldObjectMove = context.mType == Context::Global && ptr.isInCell()
                        && !ptr.getContainerStore() && destPtr.isInCell() && !destContainerIsPlayer;
                    const bool syncTes3mpWorldObjectToLocalPlayerInventory = context.mType == Context::Global
                        && ptr.isInCell() && !ptr.getContainerStore() && destContainerIsLocalPlayer;
                    const bool syncTes3mpDraftObjectMove
                        = (syncTes3mpWorldObjectMove || syncTes3mpWorldObjectToLocalPlayerInventory)
                        && ptr.getCell() == &MWBase::Environment::get().getWorldModel()->getDraftCell();
                    const bool syncTes3mpContainerObjectMove = context.mType == Context::Global && ptr.getContainerStore()
                        && !sourceContainerPtr.isEmpty() && sourceContainerPtr.isInCell() && destPtr.isInCell()
                        && !sourceContainerIsPlayer && !destContainerIsPlayer && !(sourceContainerPtr == destPtr);
                    const bool syncTes3mpLocalPlayerInventoryAdd = context.mType == Context::Global
                        && destContainerIsLocalPlayer && !sourceContainerIsPlayer
                        && ((sourceContainerPtr.isEmpty() && ptr.isInCell())
                            || (!sourceContainerPtr.isEmpty() && sourceContainerPtr.isInCell()))
                        && !(sourceContainerPtr == destPtr);
                    const bool syncTes3mpLocalPlayerInventoryRemove = context.mType == Context::Global
                        && sourceContainerIsLocalPlayer && !destContainerIsPlayer && destPtr.isInCell()
                        && !(sourceContainerPtr == destPtr);
#endif
                    std::optional<DelayedRemovalFn> delayedRemovalFn = removeFn(ptr, count);
                    context.mLuaManager->addAction([item = object, count, cont = GObject(destPtr), delayedRemovalFn
#ifdef BUILD_TES3MP_CLIENT
                                                         ,
                                                        syncTes3mpWorldObjectMove, syncTes3mpDraftObjectMove,
                                                        syncTes3mpContainerObjectMove, sourceContainerPtr,
                                                        syncTes3mpWorldObjectToLocalPlayerInventory,
                                                        syncTes3mpLocalPlayerInventoryAdd,
                                                        syncTes3mpLocalPlayerInventoryRemove, contextType = context.mType
#endif
                    ] {
                        const MWWorld::Ptr& oldPtr = item.ptr();
                        auto& refData = oldPtr.getCellRef();
                        refData.setCount(count); // temporarily undo removal to run ContainerStore::add
                        oldPtr.getRefData().enable();
#ifdef BUILD_TES3MP_CLIENT
                        if (syncTes3mpWorldObjectMove)
                            sendTes3mpLuaContainerAddPacket(cont.ptr(), oldPtr, count, contextType);
                        else if (syncTes3mpContainerObjectMove)
                        {
                            sendTes3mpLuaContainerRemovePacket(sourceContainerPtr, oldPtr, count, contextType);
                            sendTes3mpLuaContainerAddPacket(cont.ptr(), oldPtr, count, contextType);
                        }
                        else
                        {
                            if (syncTes3mpLocalPlayerInventoryRemove)
                            {
                                sendTes3mpLuaLocalPlayerInventoryPacket(
                                    sourceContainerPtr, oldPtr, count, mwmp::InventoryChanges::REMOVE, contextType);
                                sendTes3mpLuaContainerAddPacket(cont.ptr(), oldPtr, count, contextType);
                            }

                            if (syncTes3mpLocalPlayerInventoryAdd)
                            {
                                if (!sourceContainerPtr.isEmpty() && !(sourceContainerPtr == cont.ptr()))
                                    sendTes3mpLuaContainerRemovePacket(sourceContainerPtr, oldPtr, count, contextType);
                                sendTes3mpLuaLocalPlayerInventoryPacket(
                                    cont.ptr(), oldPtr, count, mwmp::InventoryChanges::ADD, contextType);
                            }
                        }
#endif
                        cont.ptr().getClass().getContainerStore(cont.ptr()).add(oldPtr, count, false);
#ifdef BUILD_TES3MP_CLIENT
                        if ((syncTes3mpWorldObjectMove || syncTes3mpWorldObjectToLocalPlayerInventory)
                            && !syncTes3mpDraftObjectMove)
                            sendTes3mpLuaObjectDeletePacket(oldPtr, contextType);
#endif
                        refData.setCount(0);
                        if (delayedRemovalFn.has_value())
                            (*delayedRemovalFn)(oldPtr);
                    });
                };
                objectT["teleport"] = [removeFn, context](const GObject& object, const sol::object& cellOrName,
                                          const osg::Vec3f& pos, const sol::object& options) {
                    MWWorld::CellStore* cell = findCell(cellOrName, pos);
                    MWWorld::Ptr ptr = object.ptr();
                    int count = ptr.getCellRef().getCount();
                    if (count == 0)
                        throw std::runtime_error("Object is either removed or already in the process of teleporting");
                    osg::Vec3f rot = ptr.getRefData().getPosition().asRotationVec3();
                    bool placeOnGround = false;
                    if (LuaUtil::isTransform(options))
                        rot = toEulerRotation(options, ptr.getClass().isActor());
                    else if (options != sol::nil)
                    {
                        sol::table t = LuaUtil::cast<sol::table>(options);
                        sol::object rotationArg = t["rotation"];
                        if (rotationArg != sol::nil)
                            rot = toEulerRotation(rotationArg, ptr.getClass().isActor());
                        placeOnGround = LuaUtil::getValueOrDefault(t["onGround"], placeOnGround);
                    }
                    if (ptr.getContainerStore())
                    {
                        DelayedRemovalFn delayedRemovalFn = *removeFn(ptr, count);
                        context.mLuaManager->addAction(
                            [object, cell, pos, rot, count, delayedRemovalFn, placeOnGround
#ifdef BUILD_TES3MP_CLIENT
                                ,
                                contextType = context.mType
#endif
                            ] {
                                MWWorld::Ptr oldPtr = object.ptr();
                                oldPtr.getCellRef().setCount(count);
                                MWWorld::Ptr newPtr = oldPtr.getClass().moveToCell(oldPtr, *cell);
                                oldPtr.getCellRef().setCount(0);
                                newPtr.getRefData().disable();
                                MWWorld::Ptr placedPtr = teleportNotPlayer(newPtr, cell, pos, rot, placeOnGround);
#ifdef BUILD_TES3MP_CLIENT
                                if (sendTes3mpLuaObjectPlacePacket(placedPtr, contextType))
                                    sendTes3mpLuaObjectRemovePacket(oldPtr, count, contextType);
#endif
                                delayedRemovalFn(oldPtr);
                            },
                            "TeleportFromContainerAction");
                    }
                    else if (ptr == MWBase::Environment::get().getWorld()->getPlayerPtr())
                        context.mLuaManager->addTeleportPlayerAction(
                            [cell, pos, rot, placeOnGround] { teleportPlayer(cell, pos, rot, placeOnGround); });
                    else
                    {
                        ptr.getCellRef().setCount(0);
                        context.mLuaManager->addAction(
                            [object, cell, pos, rot, count, placeOnGround
#ifdef BUILD_TES3MP_CLIENT
                                ,
                                syncTes3mpCrossCellTeleport = canSendTes3mpLuaObjectCrossCellTeleportPacket(
                                    ptr, cell, context.mType),
                                syncTes3mpSameCellTeleport = canSendTes3mpLuaObjectPacket(ptr, context.mType)
                                    && ptr.getCell() == cell && !placeOnGround,
                                syncTes3mpGroundedSameCellTeleport = canSendTes3mpLuaObjectGroundedSameCellTeleportPacket(
                                    ptr, cell, placeOnGround, context.mType),
                                contextType = context.mType
#endif
                            ] {
                                MWWorld::Ptr oldPtr = object.ptr();
                                oldPtr.getCellRef().setCount(count);
#ifdef BUILD_TES3MP_CLIENT
                                if (syncTes3mpCrossCellTeleport)
                                    sendTes3mpLuaObjectDeletePacket(oldPtr, contextType);
#endif
                                const bool materializesDraftObject
                                    = oldPtr.getCell() == &MWBase::Environment::get().getWorldModel()->getDraftCell();
                                MWWorld::Ptr newPtr = teleportNotPlayer(oldPtr, cell, pos, rot, placeOnGround);
#ifdef BUILD_TES3MP_CLIENT
                                if (materializesDraftObject || syncTes3mpCrossCellTeleport)
                                    sendTes3mpLuaObjectPlacePacket(newPtr, contextType);
                                else if (syncTes3mpGroundedSameCellTeleport)
                                {
                                    const ESM::Position& adjustedPosition = newPtr.getRefData().getPosition();
                                    sendTes3mpLuaObjectTeleportPacket(newPtr, newPtr.getCell(),
                                        adjustedPosition.asVec3(), adjustedPosition.asRotationVec3(), false,
                                        contextType);
                                }
                                else if (syncTes3mpSameCellTeleport)
                                {
                                    const ESM::Position& finalPosition = newPtr.getRefData().getPosition();
                                    sendTes3mpLuaObjectTeleportPacket(newPtr, newPtr.getCell(),
                                        finalPosition.asVec3(), finalPosition.asRotationVec3(), false, contextType);
                                }
#endif
                            },
                            "TeleportAction");
                    }
                };
            }
        }

        template <class ObjectT>
        void addInventoryBindings(sol::usertype<ObjectT>& objectT, const std::string& prefix, const Context& context)
        {
            using InventoryT = Inventory<ObjectT>;
            sol::usertype<InventoryT> inventoryT = context.sol().new_usertype<InventoryT>(prefix + "Inventory");

            inventoryT[sol::meta_function::to_string]
                = [](const InventoryT& inv) { return "Inventory[" + inv.mObj.toString() + "]"; };

            inventoryT["getAll"] = [ids = getPackageToTypeTable(context.mLua->unsafeState())](
                                       const InventoryT& inventory, sol::optional<sol::table> type) {
                int mask = -1;
                sol::optional<uint32_t> typeId = sol::nullopt;
                if (type.has_value())
                    typeId = ids[*type];
                else
                    mask = MWWorld::ContainerStore::Type_All;

                if (typeId.has_value())
                {
                    switch (*typeId)
                    {
                        case ESM::REC_ALCH:
                            mask = MWWorld::ContainerStore::Type_Potion;
                            break;
                        case ESM::REC_ARMO:
                            mask = MWWorld::ContainerStore::Type_Armor;
                            break;
                        case ESM::REC_BOOK:
                            mask = MWWorld::ContainerStore::Type_Book;
                            break;
                        case ESM::REC_CLOT:
                            mask = MWWorld::ContainerStore::Type_Clothing;
                            break;
                        case ESM::REC_INGR:
                            mask = MWWorld::ContainerStore::Type_Ingredient;
                            break;
                        case ESM::REC_LIGH:
                            mask = MWWorld::ContainerStore::Type_Light;
                            break;
                        case ESM::REC_MISC:
                            mask = MWWorld::ContainerStore::Type_Miscellaneous;
                            break;
                        case ESM::REC_WEAP:
                            mask = MWWorld::ContainerStore::Type_Weapon;
                            break;
                        case ESM::REC_APPA:
                            mask = MWWorld::ContainerStore::Type_Apparatus;
                            break;
                        case ESM::REC_LOCK:
                            mask = MWWorld::ContainerStore::Type_Lockpick;
                            break;
                        case ESM::REC_PROB:
                            mask = MWWorld::ContainerStore::Type_Probe;
                            break;
                        case ESM::REC_REPA:
                            mask = MWWorld::ContainerStore::Type_Repair;
                            break;
                        default:;
                    }
                }

                if (mask == -1)
                    throw std::runtime_error(
                        std::string("Incorrect type argument in inventory:getAll: " + LuaUtil::toString(*type)));

                const MWWorld::Ptr& ptr = inventory.mObj.ptr();
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                ObjectIdList list = std::make_shared<std::vector<ObjectId>>();
                auto it = store.begin(mask);
                while (it.getType() != -1)
                {
                    const MWWorld::Ptr& item = *(it++);
                    MWBase::Environment::get().getWorldModel()->registerPtr(item);
                    list->push_back(getId(item));
                }
                return ObjectList<ObjectT>{ std::move(list) };
            };

            inventoryT["countOf"] = [](const InventoryT& inventory, std::string_view recordId) {
                const MWWorld::Ptr& ptr = inventory.mObj.ptr();
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                return store.count(ESM::RefId::deserializeText(recordId));
            };
            if constexpr (std::is_same_v<ObjectT, GObject>)
            {
                inventoryT["resolve"] = [](const InventoryT& inventory) {
                    const MWWorld::Ptr& ptr = inventory.mObj.ptr();
                    MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                    store.resolve();
                };
            }
            inventoryT["isResolved"] = [](const InventoryT& inventory) -> bool {
                const MWWorld::Ptr& ptr = inventory.mObj.ptr();
                // Avoid initializing custom data
                if (!ptr.getRefData().getCustomData())
                    return false;
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                return store.isResolved();
            };
            inventoryT["find"] = [](const InventoryT& inventory, std::string_view recordId) -> sol::optional<ObjectT> {
                const MWWorld::Ptr& ptr = inventory.mObj.ptr();
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                auto itemId = ESM::RefId::deserializeText(recordId);
                for (const MWWorld::Ptr& item : store)
                {
                    if (item.getCellRef().getRefId() == itemId)
                    {
                        MWBase::Environment::get().getWorldModel()->registerPtr(item);
                        return ObjectT(getId(item));
                    }
                }
                return sol::nullopt;
            };
            inventoryT["findAll"] = [](const InventoryT& inventory, std::string_view recordId) {
                const MWWorld::Ptr& ptr = inventory.mObj.ptr();
                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                auto itemId = ESM::RefId::deserializeText(recordId);
                ObjectIdList list = std::make_shared<std::vector<ObjectId>>();
                for (const MWWorld::Ptr& item : store)
                {
                    if (item.getCellRef().getRefId() == itemId)
                    {
                        MWBase::Environment::get().getWorldModel()->registerPtr(item);
                        list->push_back(getId(item));
                    }
                }
                return ObjectList<ObjectT>{ std::move(list) };
            };
        }

        template <class ObjectT>
        void initObjectBindings(const std::string& prefix, const Context& context)
        {
            sol::usertype<ObjectT> objectT
                = context.sol().new_usertype<ObjectT>(prefix + "Object", sol::base_classes, sol::bases<Object>());
            addBasicBindings<ObjectT>(objectT, context);
            addInventoryBindings<ObjectT>(objectT, prefix, context);
            addOwnerbindings<ObjectT>(objectT, prefix, context);

            registerObjectList<ObjectT>(prefix, context);
        }
    } // namespace

    void initObjectBindingsForLocalScripts(const Context& context)
    {
        initObjectBindings<LObject>("L", context);
    }

    void initObjectBindingsForGlobalScripts(const Context& context)
    {
        initObjectBindings<GObject>("G", context);
    }

}
