#include "userdataserializer.hpp"

#include <cstring>
#include <string>

#include <components/lua/serialization.hpp>
#include <components/misc/endianness.hpp>

#include "dialogueinfo.hpp"
#include "object.hpp"

namespace MWLua
{

    class Serializer final : public LuaUtil::UserdataSerializer
    {
    public:
        explicit Serializer(bool localSerializer, std::map<int, int>* contentFileMapping)
            : mLocalSerializer(localSerializer)
            , mContentFileMapping(contentFileMapping)
        {
        }

    private:
        // Appends serialized sol::userdata to the end of BinaryData.
        // Returns false if this type of userdata is not supported by this serializer.
        bool serialize(LuaUtil::BinaryData& out, const sol::userdata& data) const override
        {
            if (data.is<GObject>() || data.is<LObject>())
            {
                appendRefNum(out, data.as<Object>().id());
                return true;
            }
            if (data.is<GObjectList>())
            {
                appendObjectIdList(out, data.as<GObjectList>().mIds);
                return true;
            }
            if (data.is<LObjectList>())
            {
                appendObjectIdList(out, data.as<LObjectList>().mIds);
                return true;
            }
            if (data.is<DialogueInfo>())
            {
                appendDialogueInfo(out, data.as<DialogueInfo>());
                return true;
            }
            return false;
        }

        constexpr static std::string_view sObjListTypeName = "objlist";
        constexpr static std::string_view sDialogueInfoTypeName = "dialinfo";

        void appendDialogueInfo(LuaUtil::BinaryData& out, const DialogueInfo& info) const
        {
            std::string payload;
            payload.push_back(static_cast<char>(info.getType()));
            payload += info.getRecordId().serializeText();
            payload.push_back('\0');
            payload += info.getInfoId().serializeText();
            append(out, sDialogueInfoTypeName, payload.data(), payload.size());
        }

        void appendObjectIdList(LuaUtil::BinaryData& out, const ObjectIdList& objList) const
        {
            static_assert(sizeof(ESM::RefNum) == 8);
            if constexpr (Misc::IS_LITTLE_ENDIAN)
                append(out, sObjListTypeName, objList->data(), objList->size() * sizeof(ESM::RefNum));
            else
            {
                std::vector<ESM::RefNum> buf;
                buf.reserve(objList->size());
                for (ESM::RefNum v : *objList)
                    buf.push_back({ Misc::toLittleEndian(v.mIndex), Misc::toLittleEndian(v.mContentFile) });
                append(out, sObjListTypeName, buf.data(), buf.size() * sizeof(ESM::RefNum));
            }
        }

        void adjustRefNum(ESM::RefNum& refNum) const
        {
            if (refNum.hasContentFile() && mContentFileMapping)
            {
                auto iter = mContentFileMapping->find(refNum.mContentFile);
                if (iter != mContentFileMapping->end())
                    refNum.mContentFile = iter->second;
            }
        }

        // Deserializes userdata of type "typeName" from binaryData. Should push the result on stack using
        // sol::stack::push. Returns false if this type is not supported by this serializer.
        bool deserialize(std::string_view typeName, std::string_view binaryData, lua_State* lua) const override
        {
            if (typeName == sRefNumTypeName)
            {
                ObjectId id = loadRefNum(binaryData);
                adjustRefNum(id);
                if (mLocalSerializer)
                    sol::stack::push<LObject>(lua, LObject(id));
                else
                    sol::stack::push<GObject>(lua, GObject(id));
                return true;
            }
            if (typeName == sObjListTypeName)
            {
                if (binaryData.size() % sizeof(ESM::RefNum) != 0)
                    throw std::runtime_error("Invalid size of ObjectIdList in MWLua::Serializer");
                ObjectIdList objList = std::make_shared<std::vector<ESM::RefNum>>();
                objList->resize(binaryData.size() / sizeof(ESM::RefNum));
                std::memcpy(objList->data(), binaryData.data(), binaryData.size());
                for (ESM::RefNum& id : *objList)
                {
                    id.mIndex = Misc::fromLittleEndian(id.mIndex);
                    id.mContentFile = Misc::fromLittleEndian(id.mContentFile);
                    adjustRefNum(id);
                }
                if (mLocalSerializer)
                    sol::stack::push<LObjectList>(lua, LObjectList{ std::move(objList) });
                else
                    sol::stack::push<GObjectList>(lua, GObjectList{ std::move(objList) });
                return true;
            }
            if (typeName == sDialogueInfoTypeName)
            {
                if (binaryData.size() < 3)
                    throw std::runtime_error("Invalid size of DialogueInfo in MWLua::Serializer");

                const auto separator = binaryData.find('\0', 1);
                if (separator == std::string_view::npos || separator == 1 || separator == binaryData.size() - 1)
                    throw std::runtime_error("Invalid DialogueInfo payload in MWLua::Serializer");

                const ESM::Dialogue::Type type = static_cast<ESM::Dialogue::Type>(binaryData[0]);
                const ESM::RefId recordId = ESM::RefId::deserializeText(binaryData.substr(1, separator - 1));
                const ESM::RefId infoId = ESM::RefId::deserializeText(binaryData.substr(separator + 1));
                sol::stack::push<DialogueInfo>(lua, DialogueInfo(type, recordId, infoId));
                return true;
            }
            return false;
        }

        bool mLocalSerializer;
        std::map<int, int>* mContentFileMapping;
    };

    std::unique_ptr<LuaUtil::UserdataSerializer> createUserdataSerializer(
        bool local, std::map<int, int>* contentFileMapping)
    {
        return std::make_unique<Serializer>(local, contentFileMapping);
    }

}
