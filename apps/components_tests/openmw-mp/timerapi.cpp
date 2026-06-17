#include <Script/API/TimerAPI.hpp>

#include <gtest/gtest.h>

namespace
{
    unsigned long long timerCallback()
    {
        return 0;
    }

    struct TimerApiTest : testing::Test
    {
        void SetUp() override { mwmp::TimerAPI::Terminate(); }
        void TearDown() override { mwmp::TimerAPI::Terminate(); }
    };

    TEST_F(TimerApiTest, reusesFreedTimerIdsWithLiveTimerStorage)
    {
        const int first = mwmp::TimerAPI::CreateTimer(timerCallback, 100000, "", {});
        mwmp::TimerAPI::StartTimer(first);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(first));

        mwmp::TimerAPI::FreeTimer(first);

        const int second = mwmp::TimerAPI::CreateTimer(timerCallback, 100000, "", {});
        EXPECT_EQ(second, first);

        mwmp::TimerAPI::StartTimer(second);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(second));
    }

    TEST_F(TimerApiTest, terminateClearsTimersAndResetsIdAllocation)
    {
        const int first = mwmp::TimerAPI::CreateTimer(timerCallback, 100000, "", {});
        EXPECT_EQ(first, 0);

        mwmp::TimerAPI::StartTimer(first);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(first));

        mwmp::TimerAPI::Terminate();

        const int second = mwmp::TimerAPI::CreateTimer(timerCallback, 100000, "", {});
        EXPECT_EQ(second, 0);

        mwmp::TimerAPI::StartTimer(second);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(second));
    }

    TEST_F(TimerApiTest, stopAndRestartPreserveElapsedStateSemantics)
    {
        const int timer = mwmp::TimerAPI::CreateTimer(timerCallback, 100000, "", {});
        EXPECT_TRUE(mwmp::TimerAPI::IsTimerElapsed(timer));

        mwmp::TimerAPI::StartTimer(timer);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(timer));

        mwmp::TimerAPI::StopTimer(timer);
        EXPECT_TRUE(mwmp::TimerAPI::IsTimerElapsed(timer));

        mwmp::TimerAPI::ResetTimer(timer, 100000);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(timer));
    }

    TEST_F(TimerApiTest, tickExpiresZeroDurationTimer)
    {
        const int timer = mwmp::TimerAPI::CreateTimer(timerCallback, 0, "", {});

        mwmp::TimerAPI::StartTimer(timer);
        EXPECT_FALSE(mwmp::TimerAPI::IsTimerElapsed(timer));

        mwmp::TimerAPI::Tick();
        EXPECT_TRUE(mwmp::TimerAPI::IsTimerElapsed(timer));
    }
}
