#include "recordidcheck.hpp"

#include <stdexcept>
#include <string>

#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/world/collectionbase.hpp>
#include <apps/opencs/model/world/record.hpp>

#include <components/esm/refid.hpp>

namespace
{
    void checkCollectionRecord(
        const CSMTools::RecordIdCheckStage::Collection& collection, int index, CSMDoc::Messages& messages)
    {
        const CSMWorld::RecordBase& record = collection.mCollection->getRecord(index);
        if (record.isDeleted())
            return;

        const ESM::RefId id = collection.mCollection->getId(index);
        const ESM::StringRefId* stringId = id.getIf<ESM::StringRefId>();
        if (stringId == nullptr)
            return;

        if (stringId->getValue().size() <= CSMTools::RecordIdCheckStage::sMaxLegacyRecordIdLength)
            return;

        messages.add(CSMWorld::UniversalId(collection.mRecordType, id),
            "Record ID is longer than 32 bytes; Morrowind and legacy ESM3 fields may truncate or reject it", "",
            CSMDoc::Message::Severity_Warning);
    }
}

CSMTools::RecordIdCheckStage::RecordIdCheckStage(std::vector<Collection> collections)
    : mCollections(std::move(collections))
{
    for (const Collection& collection : mCollections)
        if (collection.mCollection == nullptr)
            throw std::invalid_argument("record ID check collection is null");
}

int CSMTools::RecordIdCheckStage::setup()
{
    int steps = 0;
    for (const Collection& collection : mCollections)
        steps += collection.mCollection->getSize();
    return steps;
}

void CSMTools::RecordIdCheckStage::perform(int stage, CSMDoc::Messages& messages)
{
    for (const Collection& collection : mCollections)
    {
        const int size = collection.mCollection->getSize();
        if (stage < size)
        {
            checkCollectionRecord(collection, stage, messages);
            return;
        }

        stage -= size;
    }

    throw std::out_of_range("record ID check stage index out of range");
}
