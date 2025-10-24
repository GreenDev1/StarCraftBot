#pragma once

#include "Config.h"
#include "Common.h"
#include "../../BOSS/source/Timer.hpp"

namespace UAlbertaBot
{

struct Timer
{
    std::string name;
    BOSS::Timer timer;
    double highWaterMark;    // maximum milliseconds seen

    Timer();
    Timer(std::string n);

    void start()             { timer.start(); };
    void stop()              { timer.stop(); };
    double getMilliseconds() { return timer.getElapsedTimeInMilliSec(); };
};

class TimerManager
{
public:

    enum Type { Total, Information, The, OpponentModel, Search, Worker, Production, Building, Combat, Micro, Tasks, NumTypes };

private:

    std::map<Type, Timer> _timers;

    int _count;
    double _maxMilliseconds;
    double _totalMilliseconds;

    const int _barWidth;

public:

    TimerManager();

    void startTimer(TimerManager::Type t);

    void stopTimer(TimerManager::Type t);

    double getMilliseconds();      // total elapsed time
    double getMaxMilliseconds();   // over all frames
    double getMeanMilliseconds();  // over all frames

    void drawModuleTimers(int x, int y);
};

}