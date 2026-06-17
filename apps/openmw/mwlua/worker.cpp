#include "worker.hpp"

#include "luamanagerimp.hpp"

#include "apps/openmw/profile.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

#include <cassert>

namespace MWLua
{
    Worker::Worker(LuaManager& manager)
        : mManager(manager)
    {
        if (Settings::lua().mLuaNumThreads > 0)
            mThread = std::thread([this] { run(); });
    }

    Worker::~Worker()
    {
        if (mThread && mThread->joinable())
        {
            Log(Debug::Error)
                << "Unexpected destruction of LuaWorker; likely there is an unhandled exception in the main thread.";
            join();
        }
    }

    void Worker::allowUpdate(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats)
    {
        if (!mThread)
            return;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            assert(!mRequest.has_value());
            mRequest = Request{
                .mOperation = Operation::Update,
                .mFrameStart = frameStart,
                .mFrameNumber = frameNumber,
                .mStats = &stats,
            };
        }
        mCV.notify_one();
    }

    void Worker::finishUpdate(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats)
    {
        if (mThread)
        {
            std::unique_lock<std::mutex> lk(mMutex);
            assert(!mRequest.has_value() || mRequest->mOperation == Operation::Update);
            mCV.wait(lk, [&] { return !mRequest.has_value(); });
        }
        else
            update(frameStart, frameNumber, stats);
    }

    void Worker::gc(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats)
    {
        if (!mThread)
            return;
        if (Settings::lua().mGcStepsPerFrame <= 0)
            return;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            assert(!mRequest.has_value());
            mGcStopRequest = false;
            mRequest = Request{
                .mOperation = Operation::Gc,
                .mFrameStart = frameStart,
                .mFrameNumber = frameNumber,
                .mStats = &stats,
            };
        }
        mCV.notify_one();
    }

    void Worker::finishGc(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats)
    {
        if (Settings::lua().mGcStepsPerFrame <= 0)
            return;

        if (mThread)
        {
            std::unique_lock<std::mutex> lk(mMutex);
            if (!mRequest.has_value())
                return;
            assert(mRequest->mOperation == Operation::Gc);
            mGcStopRequest = true;
            mCV.wait(lk, [&] { return !mRequest.has_value(); });
        }
        else
            collectGarbage(frameStart, frameNumber, stats, false);
    }

    void Worker::join()
    {
        if (mThread)
        {
            {
                std::lock_guard<std::mutex> lk(mMutex);
                mJoinRequest = true;
            }
            mCV.notify_one();
            mThread->join();
        }
    }

    void Worker::update(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats)
    {
        const osg::Timer* const timer = osg::Timer::instance();
        OMW::ScopedProfile<OMW::UserStatsType::Lua> profile(frameStart, frameNumber, *timer, stats);

        mManager.update();
    }

    void Worker::collectGarbage(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats, bool untilStopped)
    {
        const osg::Timer* const timer = osg::Timer::instance();
        OMW::ScopedProfile<OMW::UserStatsType::LuaGc> profile(frameStart, frameNumber, *timer, stats);

        do
        {
            if (!mManager.gc())
                break;

            if (!untilStopped)
                break;

            std::lock_guard<std::mutex> lk(mMutex);
            if (isGcStopRequested())
                break;
        } while (true);
    }

    bool Worker::isGcStopRequested() const
    {
        return mGcStopRequest || mJoinRequest;
    }

    void Worker::run() noexcept
    {
        while (true)
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mCV.wait(lk, [&] { return mRequest.has_value() || mJoinRequest; });
            if (mJoinRequest)
                break;

            assert(mRequest.has_value());
            const Request request = *mRequest;
            lk.unlock();

            try
            {
                switch (request.mOperation)
                {
                    case Operation::Gc:
                        collectGarbage(request.mFrameStart, request.mFrameNumber, *request.mStats, true);
                        break;
                    case Operation::Update:
                        update(request.mFrameStart, request.mFrameNumber, *request.mStats);
                        break;
                }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to process LuaWorker request: " << e.what();
            }

            lk.lock();
            mRequest.reset();
            mGcStopRequest = false;
            lk.unlock();
            mCV.notify_one();
        }
    }
}
