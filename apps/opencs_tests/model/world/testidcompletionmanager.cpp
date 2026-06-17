#include "apps/opencs/model/world/idcompletionmanager.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace CSMWorld
{
    namespace
    {
        using namespace ::testing;

        TEST(CSMWorldIdCompletionManagerTest, exposesInteriorCellDisplayType)
        {
            EXPECT_TRUE(ColumnBase::isId(ColumnBase::Display_InteriorCell));
            EXPECT_THAT(IdCompletionManager::getDisplayTypes(), Contains(ColumnBase::Display_InteriorCell));
        }
    }
}
