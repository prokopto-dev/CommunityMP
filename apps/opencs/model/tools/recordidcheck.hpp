#ifndef CSM_TOOLS_RECORDIDCHECK_H
#define CSM_TOOLS_RECORDIDCHECK_H

#include <cstddef>
#include <vector>

#include "../doc/stage.hpp"
#include "../world/universalid.hpp"

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class CollectionBase;
}

namespace CSMTools
{
    /// \brief Verify stage: warn about record IDs that exceed legacy ESM3/Morrowind limits.
    class RecordIdCheckStage : public CSMDoc::Stage
    {
    public:
        struct Collection
        {
            const CSMWorld::CollectionBase* mCollection;
            CSMWorld::UniversalId::Type mRecordType;
        };

        static constexpr std::size_t sMaxLegacyRecordIdLength = 32;

    private:
        std::vector<Collection> mCollections;

    public:
        explicit RecordIdCheckStage(std::vector<Collection> collections);

        int setup() override;
        ///< \return number of steps

        void perform(int stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };
}

#endif
