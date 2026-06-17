#ifndef MWLUA_DIALOGUEINFO_H
#define MWLUA_DIALOGUEINFO_H

#include <components/esm/refid.hpp>
#include <components/esm3/loaddial.hpp>

namespace MWLua
{
    class DialogueInfo
    {
    public:
        DialogueInfo(ESM::Dialogue::Type type, ESM::RefId recordId, ESM::RefId infoId);
        DialogueInfo(const ESM::Dialogue& record, const ESM::DialInfo& info);

        ESM::Dialogue::Type getType() const { return mType; }
        const ESM::RefId& getRecordId() const { return mRecordId; }
        const ESM::RefId& getInfoId() const { return mInfoId; }

        const ESM::Dialogue* getRecord() const;
        const ESM::DialInfo* getInfo() const;
        const ESM::DialInfo& requireInfo() const;

    private:
        ESM::Dialogue::Type mType;
        ESM::RefId mRecordId;
        ESM::RefId mInfoId;
        mutable const ESM::DialInfo* mInfo = nullptr;
    };
}

#endif // MWLUA_DIALOGUEINFO_H
