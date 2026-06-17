#include "TimerAPI.hpp"

#include <iostream>
using namespace mwmp;

Timer::Timer(ScriptFunc callback, long msec, const std::string& def, std::vector<boost::any> args) : ScriptFunction(callback, 'v', def)
{
    targetDuration = std::chrono::milliseconds(msec);
    this->args = args;
    isEnded = true;
}

#if defined(ENABLE_LUA)
Timer::Timer(lua_State *lua, ScriptFuncLua callback, long msec, const std::string& def, std::vector<boost::any> args): ScriptFunction(callback, lua, 'v', def)
{
    targetDuration = std::chrono::milliseconds(msec);
    this->args = args;
    isEnded = true;
}
#endif

void Timer::Tick()
{
    if (isEnded)
        return;

    if (std::chrono::steady_clock::now() - startTime >= targetDuration)
    {
        isEnded = true;
        Call(args);
    }
}

bool Timer::IsEnded()
{
    return isEnded;
}

void Timer::Stop()
{
    isEnded = true;
}

void Timer::Restart(int msec)
{
    targetDuration = std::chrono::milliseconds(msec);
    Start();
}

void Timer::Start()
{
    isEnded = false;
    startTime = std::chrono::steady_clock::now();
}

int TimerAPI::pointer = 0;
std::unordered_map<int, Timer* > TimerAPI::timers;

#if defined(ENABLE_LUA)
int TimerAPI::CreateTimerLua(lua_State *lua, ScriptFuncLua callback, long msec, const std::string& def, std::vector<boost::any> args)
{
    int id = -1;

    for (auto& timer : timers)
    {
        if (timer.second != nullptr)
            continue;
        timer.second = new Timer(lua, callback, msec, def, args);
        id = timer.first;
        break;
    }

    if (id == -1)
    {
        timers[pointer] = new Timer(lua, callback, msec, def, args);
        id = pointer;
        pointer++;
    }

    return id;
}
#endif


int TimerAPI::CreateTimer(ScriptFunc callback, long msec, const std::string &def, std::vector<boost::any> args)
{
    int id = -1;

    for (auto& timer : timers)
    {
        if (timer.second != nullptr)
            continue;
        timer.second = new Timer(callback, msec, def, args);
        id = timer.first;
        break;
    }

    if (id == -1)
    {
        timers[pointer] = new Timer(callback, msec, def, args);
        id = pointer;
        pointer++;
    }

    return id;
}

void TimerAPI::FreeTimer(int timerid)
{

    try
    {
        if (timers.at(timerid) != nullptr)
        {
            delete timers[timerid];
            timers[timerid] = nullptr;
        }
    }
    catch(...)
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
    }
}

void TimerAPI::ResetTimer(int timerid, long msec)
{
    try
    {
        timers.at(timerid)->Restart(msec);
    }
    catch(...)
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
    }
}

void TimerAPI::StartTimer(int timerid)
{
    try
    {
        Timer *timer = timers.at(timerid);
        if (timer == nullptr)
            throw 1;
        timer->Start();
    }
    catch(...)
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
    }
}

void TimerAPI::StopTimer(int timerid)
{
    try
    {
        timers.at(timerid)->Stop();
    }
    catch(...)
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
    }
}

bool TimerAPI::IsTimerElapsed(int timerid)
{
    bool ret = false;
    try
    {
        ret = timers.at(timerid)->IsEnded();
    }
    catch(...)
    {
        std::cerr << "Timer " << timerid << " not found!" << std::endl;
    }
    return ret;
}

void TimerAPI::Terminate()
{
    for (auto& timer : timers)
    {
        if (timer.second != nullptr)
            delete timer.second;
        timer.second = nullptr;
    }
    timers.clear();
    pointer = 0;
}

void TimerAPI::Tick()
{
    for (auto timer : timers)
    {
        if (timer.second != nullptr)
            timer.second->Tick();
    }
}
