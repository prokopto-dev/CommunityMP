#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/tools/recordidcheck.hpp>
#include <apps/opencs/model/world/collectionbase.hpp>
#include <apps/opencs/model/world/record.hpp>

#include <components/esm/refid.hpp>

#include <gtest/gtest.h>

#include <QVariant>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class TestRecord final : public CSMWorld::RecordBase
    {
    public:
        explicit TestRecord(State state)
            : RecordBase(state)
        {
        }

        std::unique_ptr<CSMWorld::RecordBase> clone() const override { return std::make_unique<TestRecord>(mState); }

        std::unique_ptr<CSMWorld::RecordBase> modifiedCopy() const override
        {
            return std::make_unique<TestRecord>(State_ModifiedOnly);
        }

        void assign(const CSMWorld::RecordBase& record) override { mState = record.mState; }
    };

    class TestCollection final : public CSMWorld::CollectionBase
    {
    public:
        struct Entry
        {
            ESM::RefId mId;
            std::unique_ptr<TestRecord> mRecord;
        };

        std::vector<Entry> mEntries;

        void add(ESM::RefId id, CSMWorld::RecordBase::State state = CSMWorld::RecordBase::State_ModifiedOnly)
        {
            mEntries.push_back({ std::move(id), std::make_unique<TestRecord>(state) });
        }

        int getSize() const override { return static_cast<int>(mEntries.size()); }

        ESM::RefId getId(int index) const override { return mEntries.at(index).mId; }

        int getIndex(const ESM::RefId& id) const override
        {
            const int index = searchId(id);
            if (index == -1)
                throw std::runtime_error("ID is not found in test collection");
            return index;
        }

        int getColumns() const override { throw std::logic_error("not implemented"); }

        const CSMWorld::ColumnBase& getColumn(int column) const override { throw std::logic_error("not implemented"); }

        QVariant getData(int index, int column) const override { throw std::logic_error("not implemented"); }

        void setData(int index, int column, const QVariant& data) override
        {
            throw std::logic_error("not implemented");
        }

        void removeRows(int index, int count) override { throw std::logic_error("not implemented"); }

        void appendBlankRecord(const ESM::RefId& id, CSMWorld::UniversalId::Type type) override
        {
            throw std::logic_error("not implemented");
        }

        int searchId(const ESM::RefId& id) const override
        {
            for (std::size_t i = 0; i < mEntries.size(); ++i)
                if (mEntries[i].mId == id)
                    return static_cast<int>(i);
            return -1;
        }

        void replace(int index, std::unique_ptr<CSMWorld::RecordBase> record) override
        {
            throw std::logic_error("not implemented");
        }

        void appendRecord(std::unique_ptr<CSMWorld::RecordBase> record, CSMWorld::UniversalId::Type type) override
        {
            throw std::logic_error("not implemented");
        }

        void cloneRecord(
            const ESM::RefId& origin, const ESM::RefId& destination, const CSMWorld::UniversalId::Type type) override
        {
            throw std::logic_error("not implemented");
        }

        bool touchRecord(const ESM::RefId& id) override { throw std::logic_error("not implemented"); }

        const CSMWorld::RecordBase& getRecord(const ESM::RefId& id) const override { return getRecord(getIndex(id)); }

        const CSMWorld::RecordBase& getRecord(int index) const override { return *mEntries.at(index).mRecord; }

        int getAppendIndex(const ESM::RefId& id, CSMWorld::UniversalId::Type type) const override
        {
            throw std::logic_error("not implemented");
        }

        std::vector<ESM::RefId> getIds(bool listDeleted = true) const override
        {
            std::vector<ESM::RefId> ids;
            for (const Entry& entry : mEntries)
                if (listDeleted || !entry.mRecord->isDeleted())
                    ids.push_back(entry.mId);
            return ids;
        }

        bool reorderRows(int baseIndex, const std::vector<int>& newOrder) override
        {
            throw std::logic_error("not implemented");
        }
    };

    std::vector<CSMDoc::Message> runRecordIdCheck(const TestCollection& collection)
    {
        CSMTools::RecordIdCheckStage stage({ { &collection, CSMWorld::UniversalId::Type_Global } });
        CSMDoc::Messages messages(CSMDoc::Message::Severity_Error);

        const int steps = stage.setup();
        for (int i = 0; i < steps; ++i)
            stage.perform(i, messages);

        return { messages.begin(), messages.end() };
    }
}

TEST(CSMToolsRecordIdCheckStage, shouldWarnAboutStringRecordIdsLongerThanThirtyTwoBytes)
{
    TestCollection collection;
    collection.add(ESM::RefId::stringRefId(std::string(32, 'a')));
    collection.add(ESM::RefId::stringRefId(std::string(33, 'b')));

    const std::vector<CSMDoc::Message> messages = runRecordIdCheck(collection);

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.front().mId.getRefId(), ESM::RefId::stringRefId(std::string(33, 'b')));
    EXPECT_EQ(messages.front().mSeverity, CSMDoc::Message::Severity_Warning);
    EXPECT_EQ(messages.front().mMessage,
        "Record ID is longer than 32 bytes; Morrowind and legacy ESM3 fields may truncate or reject it");
}

TEST(CSMToolsRecordIdCheckStage, shouldIgnoreDeletedAndNonStringRecordIds)
{
    TestCollection collection;
    collection.add(ESM::RefId::stringRefId(std::string(33, 'c')), CSMWorld::RecordBase::State_Deleted);
    collection.add(ESM::RefId::index(ESM::REC_SKIL, 0));

    const std::vector<CSMDoc::Message> messages = runRecordIdCheck(collection);

    EXPECT_TRUE(messages.empty());
}
