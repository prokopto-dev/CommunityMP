#ifndef OPENMW_MWLUA_WORKER_H
#define OPENMW_MWLUA_WORKER_H

#include <osg/Timer>
#include <osg/ref_ptr>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace osg
{
    class Stats;
}

namespace MWLua
{
    class LuaManager;

    class Worker
    {
    public:
        explicit Worker(LuaManager& manager);

        ~Worker();

        void allowUpdate(osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats);

        void finishUpdate(osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats);

        void gc(osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats);

        void finishGc(osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats);

        void join();

    private:
        enum class Operation
        {
            Gc,
            Update,
        };

        struct Request
        {
            Operation mOperation;
            osg::Timer_t mFrameStart;
            unsigned mFrameNumber;
            osg::ref_ptr<osg::Stats> mStats;
        };

        void update(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats);

        void collectGarbage(osg::Timer_t frameStart, unsigned frameNumber, osg::Stats& stats, bool untilStopped);

        bool isGcStopRequested() const;

        void run() noexcept;

        LuaManager& mManager;
        std::mutex mMutex;
        std::condition_variable mCV;
        std::optional<Request> mRequest;
        bool mGcStopRequest = false;
        bool mJoinRequest = false;
        std::optional<std::thread> mThread;
    };
}

#endif // OPENMW_MWLUA_LUAWORKER_H
