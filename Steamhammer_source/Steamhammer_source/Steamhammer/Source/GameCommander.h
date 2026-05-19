#pragma once

#include <BWAPI.h>
#include "CombatCommander.h"
#include "TimerManager.h"

namespace UAlbertaBot
{
class UnitToAssign
{
public:

    BWAPI::Unit unit;
    bool isAssigned;

    UnitToAssign(BWAPI::Unit u)
    {
        unit = u;
        isAssigned = false;
    }
};

class GameCommander 
{
    struct Record {
        int    frame;
        bool   isWin;
        std::string map;
        std::string race;
        bool   didExpand;
    };

    CombatCommander &	_combatCommander;
    TimerManager		_timerManager;

    BWAPI::Unitset      _validUnits;
    BWAPI::Unitset      _combatUnits;
    BWAPI::Unitset      _scoutUnits;
    BWAPI::Unitset      _distractorUnits;
    BWAPI::Unitset      _transportUnits;

    int					_surrenderTime;     // for giving up early
    int                 _myHighWaterSupply; // used for surrender decisions vs. a human

    int					_initialScoutTime;  // 0 until a scouting worker is assigned
    int					_initialDistractorTime;  // 0 until a distractor unit is assigned
    bool                _doExpand;

    static constexpr double LEARNING_RATE = 0.3;
    static constexpr int FEATURE_COUNT = 6;
    static constexpr double DECAY_RATE = 0.95;

    void                assignUnit(BWAPI::Unit unit, BWAPI::Unitset & set);
    bool                isAssigned(BWAPI::Unit unit) const;

    bool				surrenderMonkey();

    BWAPI::Unit         getAnyFreeWorker();
    BWAPI::Unit         getAnyDistractor();

    void                drawDebugInterface();
    void                drawGameInformation(int x, int y);
    void                drawUnitOrders();
    void                drawUnitCounts(int x, int y) const;
    void                drawTerrainHeights() const;
    void                drawDefenseClusters();

    std::pair<double, int> getStrategyStats(const std::string& mapName, bool wantExpandData);

    bool                shouldExpandRuleBased(std::vector<Record>);
    bool                shouldExpandML(std::string opponentName);
    void trainModel(const std::string& opponentName, ...);
    void recordMatchResult(std::string opponentName, int attackFrame, bool won, std::string mapName, bool expanded);
    static std::vector<double> loadWeights(const std::string& opponentName);
    static std::vector<Record> loadRecords(std::string opponentName);

    std::vector<Record> _history; // Tu budeme drûaù naËÌtanÈ d·ta
    std::vector<double> _weights; // Tu budeme drûaù v·hy modelu

    double calculateAvgAttackFrame(const std::vector<Record>& history, const std::string& enemyRace);


public:

    GameCommander();
    ~GameCommander() {};

    void update();
    bool shouldExpand(const std::string& opponentName);



    void onStart();


    void onEnd(bool isWinner);

    void handleUnitAssignments();
    void setValidUnits();
    void setScoutUnits();
    void setDistractorUnits();
    void setTransportUnits();
    void setCombatUnits();

    void releaseOverlord(BWAPI::Unit overlord);     // zerg scouting overlord

    int getScoutTime() const { return _initialScoutTime; };

    void onUnitShow(BWAPI::Unit unit);
    void onUnitHide(BWAPI::Unit unit);
    void onUnitCreate(BWAPI::Unit unit);
    void onUnitComplete(BWAPI::Unit unit);
    void onUnitRenegade(BWAPI::Unit unit);
    void onUnitDestroy(BWAPI::Unit unit);
    void onUnitMorph(BWAPI::Unit unit);

    static GameCommander & Instance();
};

}