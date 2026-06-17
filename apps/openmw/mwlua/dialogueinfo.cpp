#include "dialogueinfo.hpp"

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/store.hpp"

#include <stdexcept>
#include <utility>

namespace MWLua
{
    DialogueInfo::DialogueInfo(ESM::Dialogue::Type type, ESM::RefId recordId, ESM::RefId infoId)
        : mType(type)
        , mRecordId(std::move(recordId))
        , mInfoId(std::move(infoId))
    {
    }

    DialogueInfo::DialogueInfo(const ESM::Dialogue& record, const ESM::DialInfo& info)
        : mType(record.mType)
        , mRecordId(record.mId)
        , mInfoId(info.mId)
        , mInfo(&info)
    {
    }

    const ESM::Dialogue* DialogueInfo::getRecord() const
    {
        const ESM::Dialogue* record = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().search(mRecordId);
        if (record == nullptr || record->mType != mType)
            return nullptr;
        return record;
    }

    const ESM::DialInfo* DialogueInfo::getInfo() const
    {
        if (mInfo != nullptr)
            return mInfo;

        const ESM::Dialogue* record = getRecord();
        if (record == nullptr)
            return nullptr;

        for (const ESM::DialInfo& info : record->mInfo)
        {
            if (info.mId == mInfoId)
            {
                mInfo = &info;
                return mInfo;
            }
        }

        return nullptr;
    }

    const ESM::DialInfo& DialogueInfo::requireInfo() const
    {
        const ESM::DialInfo* info = getInfo();
        if (info == nullptr)
            throw std::runtime_error("Dialogue info is not available");
        return *info;
    }
}
