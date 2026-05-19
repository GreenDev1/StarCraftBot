#include "GameCommander.h"

#include "Bases.h"
#include "BuildingPlacer.h"
#include "MapTools.h"
#include "OpponentModel.h"
#include "UnitUtil.h"

#include "BuildingManager.h"
#include "InformationManager.h"
#include "MapGrid.h"
#include "OpeningTiming.h"
#include "OpponentModel.h"
#include "ProductionManager.h"
#include "ScoutManager.h"
#include "StrategyManager.h"
#include "WorkerManager.h"
#include "DistractorManager.h"
#include "TransportManager.h"
#include "StrategyBossZerg.h"

using namespace UAlbertaBot;

GameCommander::GameCommander() 
    : _combatCommander(CombatCommander::Instance())
    , _initialScoutTime(0)
    , _surrenderTime(0)
    , _myHighWaterSupply(0)
{
}

void GameCommander::update()
{
    _timerManager.startTimer(TimerManager::Total);

    // populate the unit vectors we will pass into various managers
    handleUnitAssignments();

    // Decide whether to give up early. Implements config option SurrenderWhenHopeIsLost.
    if (!_surrenderTime && surrenderMonkey())
    {
        _surrenderTime = the.now();
        GameMessage("gg");
    }
    if (_surrenderTime)
    {
        if (the.now() - _surrenderTime >= 36)  // 36 frames = 1.5 game seconds
        {
            BWAPI::Broodwar->leaveGame();
        }
        _timerManager.stopTimer(TimerManager::Total);
        return;
    }

    // -- Managers that gather information. --

    _timerManager.startTimer(TimerManager::Information);
    Bases::Instance().update();
    InformationManager::Instance().update();
    _timerManager.stopTimer(TimerManager::Information);

    _timerManager.startTimer(TimerManager::The);
    the.update();
    _timerManager.stopTimer(TimerManager::The);

    _timerManager.startTimer(TimerManager::OpponentModel);
    OpponentModel::Instance().update();
    the.skillkit.update();
    _timerManager.stopTimer(TimerManager::OpponentModel);

    // -- Managers that act on information. --

    _timerManager.startTimer(TimerManager::Search);
    BOSSManager::Instance().update(35 - _timerManager.getMilliseconds());
    _timerManager.stopTimer(TimerManager::Search);

    // May steal workers from WorkerManager, so run it before WorkerManager.
    _timerManager.startTimer(TimerManager::Production);
    ProductionManager::Instance().update();
    _timerManager.stopTimer(TimerManager::Production);

    // May steal workers from WorkerManager, so run it before WorkerManager.
    _timerManager.startTimer(TimerManager::Building);
    BuildingManager::Instance().update();
    _timerManager.stopTimer(TimerManager::Building);

    _timerManager.startTimer(TimerManager::Worker);
    WorkerManager::Instance().update();
    _timerManager.stopTimer(TimerManager::Worker);

    _timerManager.startTimer(TimerManager::Combat);
    _combatCommander.update(_combatUnits);
    ScoutManager::Instance().update();      // uses little time
    // distractor update
    DistractorManager::Instance().update();
    // TransportManager::Instance().update(); update prebieha v productionManager
    _timerManager.stopTimer(TimerManager::Combat);

    // Execute micro commands gathered above.
    _timerManager.startTimer(TimerManager::Micro);
    the.micro.update();
    _timerManager.stopTimer(TimerManager::Micro);

    // Last so that it can delay tasks that may take too long. (NOTE Not yet implemented.)
    _timerManager.startTimer(TimerManager::Tasks);
    the.tasks.update();
    _timerManager.stopTimer(TimerManager::Tasks);

    _timerManager.stopTimer(TimerManager::Total);

    drawDebugInterface();
}

std::pair<double, int> UAlbertaBot::GameCommander::getStrategyStats(const std::string& mapName, bool wantExpandData)
{
    double wins = 0;
    int games = 0;

    for (const auto& rec : _history)
    {
        // Filtrujeme len záznamy pre túto mapu a konkrétnu stratégiu
        if (rec.map == mapName && rec.didExpand == wantExpandData)
        {
            games++;
            if (rec.isWin) wins++;
        }
    }

    // Ak nemáme žiadne hry, vrátime "neutrálnych" 50% winrate
    if (games == 0) return { 0.5, 0 };

    return { wins / (double)games, games };
}

void GameCommander::onStart() {
    std::string opponentName = BWAPI::Broodwar->enemy()->getName();

    // 1. Naèítaj históriu zápasov (z read/)
    _history = loadRecords(opponentName);

    // 2. Naèítaj váhy (z read/)
    _weights = loadWeights(opponentName);

    BWAPI::Broodwar->printf("Nacitanych %d predchadzajucich zapasov.", _history.size());
}

std::string getSanitizedFileName(const std::string& opponentName) {
    std::string name = opponentName;
    std::replace(name.begin(), name.end(), ' ', '_');
    // Pridáme prefix "mr_", aby sme dodržali formát tvojho OpponentModelu
    return name;
}

static double sigmoid(double x) {
    return 1.0 / (1.0 + std::exp(-x));
}

static double dotProduct(const std::vector<double>& w, const std::vector<double>& f) {
    double sum = 0.0;
    for (int i = 0; i < (int)w.size(); i++)
        sum += w[i] * f[i];
    return sum;
}

double GameCommander::calculateAvgAttackFrame(const std::vector<Record>& history, const std::string& enemyRace) {
    double totalWeightedFrame = 0.0;
    double totalWeight = 0.0;
    double currentDecay = 1.0;
    const double DECAY_RATE = 0.9; // Novšie hry majú väèšiu váhu
    const double MAX_FRAME = 25000.0;

    // Od najnovších po najstaršie
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->race != enemyRace) continue;

        double frameValue = (it->frame == -1) ? MAX_FRAME : (double)it->frame;
        totalWeightedFrame += frameValue * currentDecay;
        totalWeight += currentDecay;
        currentDecay *= DECAY_RATE;
    }

    if (totalWeight == 0) return 1.0; // Ak nemáme dáta, predpokladáme neskorý útok (bezpeèné)

    double avgFrame = totalWeightedFrame / totalWeight;
    return std::min(avgFrame / MAX_FRAME, 1.0); // Normalizácia 0.0 až 1.0
}

// FEATURE_COUNT = 6
static std::vector<double> buildFeatures(
    bool didExpand,
    double avgAttackFrameNorm,
    double wrExpand,
    double wrNoExpand,
    int gamesOnMap)
{
    double normGames = std::min(gamesOnMap / 10.0, 1.0);

    return {
        1.0,                        // [0] Bias
        didExpand ? 1.0 : 0.0,      // [1] Rozhodnutie o expanzii
        avgAttackFrameNorm,         // [2] Priemerný èas útoku (0.0 - 1.0)
        wrExpand,                   // [3] Úspešnos expanzie na mape
        wrNoExpand,                 // [4] Úspešnos 1-base na mape
        normGames                   // [5] Poèet hier na mape (dôveryhodnos dát)
    };
}

// Cesta pre váhy - VŽDY do write, pretože odtia¾ to server po hre vezme
static std::string weightsPath(const std::string& opponentName) {
    return "bwapi-data/write/mr_"
        + getSanitizedFileName(opponentName) + "_"
        + BWAPI::Broodwar->enemy()->getRace().getName()
        + "_weights.txt";
}

std::vector<double> GameCommander::loadWeights(const std::string& opponentName)
{
    std::string sanitizedName = getSanitizedFileName(opponentName);
    std::string sanitizedRace = BWAPI::Broodwar->enemy()->getRace().getName();  // chýbalo!

    std::vector<std::string> paths = {
        "bwapi-data/write/mr_" + sanitizedName + "_" + sanitizedRace + "_weights.txt",
        "bwapi-data/read/mr_" + sanitizedName + "_" + sanitizedRace + "_weights.txt"
    };
    for (const auto& path : paths) {
        std::ifstream f(path);
        if (f.is_open()) {
            std::vector<double> weights(FEATURE_COUNT, 0.0);
            for (int i = 0; i < FEATURE_COUNT; ++i)
                if (!(f >> weights[i])) break;
            return weights;
        }
    }
    return std::vector<double>(FEATURE_COUNT, 0.0);
}

static void saveWeights(const std::string& opponentName,
    const std::vector<double>& weights)
{
    std::ofstream f(weightsPath(opponentName));
    for (double w : weights)
        f << w << "\n";
}

void GameCommander::trainModel(const std::string& opponentName, ...)
{
    std::string currentRace = BWAPI::Broodwar->enemy()->getRace().getName();
    double decayFactor = 1.0;

    // Cache pre mapy: aby sme getStrategyStats nevolali pre každú hru na tej istej mape
    struct MapCache { double wrEx; int gEx; double wrNo; int gNo; };
    std::map<std::string, MapCache> mapCache;

    for (int j = static_cast<int>(_history.size()) - 1; j >= 0; --j) {
        const auto& rec = _history[j];
        if (rec.race != currentRace) continue;

        double currentGameFrame = (rec.frame == -1) ? MAX_FRAME : static_cast<double>(rec.frame);
        double normFrame = std::min(currentGameFrame / MAX_FRAME, 1.0);

        // 2. ZÍSKANIE ŠTATISTÍK MAPY (Efektívne cez cache)
        if (mapCache.find(rec.map) == mapCache.end()) {
            auto [wrEx, gEx] = getStrategyStats(rec.map, true);
            auto [wrNo, gNo] = getStrategyStats(rec.map, false);
            mapCache[rec.map] = { wrEx, gEx, wrNo, gNo };
        }

        MapCache& mc = mapCache[rec.map];

        // 3. VYTVORENIE FEATURES (Teraz je to bleskové)
        auto features = buildFeatures(rec.didExpand, normFrame, mc.wrEx, mc.wrNo, mc.gEx + mc.gNo);

        // 4. ZVYŠOK OSTÁVA (Update váh)
        double predicted = sigmoid(dotProduct(_weights, features));
        double actual = rec.isWin ? 1.0 : 0.0;
        double error = actual - predicted;

        for (int i = 0; i < FEATURE_COUNT; i++) {
            _weights[i] += LEARNING_RATE * decayFactor * error * features[i];
        }

        decayFactor *= DECAY_RATE;
    }

    saveWeights(opponentName, _weights);
}

void GameCommander::recordMatchResult(std::string opponentName, int attackFrame, bool won, std::string mapName, bool expanded) {
    // 1. Príprava nového záznamu
    Record newRecord;
    newRecord.frame = attackFrame;
    newRecord.isWin = won;
    newRecord.map = mapName;
    std::replace(newRecord.map.begin(), newRecord.map.end(), ';', '_'); // Ošetrenie stredníka
    newRecord.race = BWAPI::Broodwar->enemy()->getRace().getName();
    newRecord.didExpand = expanded;

    // 2. Pridanie do histórie (predpokladáme, že _history je èlen triedy naèítaný v onStart)
    _history.push_back(newRecord);

    // 3. Limit 150 záznamov
    if (_history.size() > 150) {
        _history.erase(_history.begin(), _history.begin() + (_history.size() - 150));
    }

    // 4. Zápis do WRITE
    std::string fileName = "bwapi-data/write/mr_" + getSanitizedFileName(opponentName) + ".txt";
    std::ofstream outputFile(fileName, std::ios::trunc);

    if (outputFile.is_open()) {
        for (const auto& r : _history) {
            outputFile << r.frame << ";"
                << (r.isWin ? "WIN" : "LOSS") << ";"
                << r.map << ";"
                << r.race << ";"
                << (r.didExpand ? "1" : "0") << "\n";
        }
        outputFile.close();
    }
}

std::vector<GameCommander::Record> GameCommander::loadRecords(std::string opponentName) {
    std::vector<Record> records;

    std::replace(opponentName.begin(), opponentName.end(), ' ', '_');
    std::string path = "bwapi-data/read/mr_" + opponentName + ".txt";

    std::ifstream inFile(path);
    if (!inFile.is_open()) return records; // Ak súbor neexistuje, vrátime prázdny zoznam

    std::string line;
    while (std::getline(inFile, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ss, token, ';')) tokens.push_back(token);

        if (tokens.size() >= 5) {
            Record r;
            r.frame = std::stoi(tokens[0]);
            r.isWin = (tokens[1] == "WIN");
            r.map = tokens[2];
            r.race = tokens[3];
            r.didExpand = (tokens[4] == "1");
            records.push_back(r);
        }
    }
    inFile.close();
    return records;
}

// Odporúèané volanie: shouldExpand(opponentName, BWAPI::Broodwar->mapName(), BWAPI::Broodwar->enemy()->getRace().getName())

bool GameCommander::shouldExpand(const std::string& opponentName)
{
    std::string currentRace = BWAPI::Broodwar->enemy()->getRace().getName();

    int relevantGames = 0;
    for (const auto& rec : _history)
        if (rec.race == currentRace) ++relevantGames;

    if (relevantGames < 5)
        return shouldExpandRuleBased(_history);

    return shouldExpandML(opponentName);
}

void GameCommander::onEnd(bool isWinner)
{
    // 1. Získanie potrebných dát
    std::string opponentName = BWAPI::Broodwar->enemy()->getName();
    std::string mapName = BWAPI::Broodwar->mapFileName(); // alebo mapName() pre pekný názov

    // 2. Získanie èasu útoku zo StrategyBossZerg
    // Predpokladám, že tvoje premenné sú prístupné cez Instance() alebo ich máš v StrategyManageri
    int attackFrame = StrategyBossZerg::Instance().getFirstAttackTime();
    bool didExpand = StrategyBossZerg::Instance().getDidExpand();

    // 3. Zápis do tvojho súboru
    recordMatchResult(opponentName, attackFrame, isWinner, mapName, didExpand);
    trainModel(opponentName, didExpand, isWinner, mapName, BWAPI::Broodwar->enemy()->getRace().getName(), attackFrame);

    OpponentModel::Instance().setWin(isWinner);
    OpponentModel::Instance().write();

    // Clean up any data structures that may otherwise not be unwound in the correct order.
    // This fixes an end-of-game bug diagnosed by Bruce Nielsen.
    _combatCommander.onEnd();
}

void GameCommander::drawDebugInterface()
{
    InformationManager::Instance().drawExtendedInterface();
    InformationManager::Instance().drawUnitInformation(425,30);
    drawUnitCounts(345, 30);
    Bases::Instance().drawBaseInfo();
    Bases::Instance().drawBaseOwnership(575, 30);
    the.map.drawExpoScores();
    InformationManager::Instance().drawResourceAmounts();
    BuildingManager::Instance().drawBuildingInformation(200, 50);
    the.placer.drawReservedTiles();
    the.tasks.draw(200, 50);
    ProductionManager::Instance().drawProductionInformation(30, 60);
    BOSSManager::Instance().drawSearchInformation(490, 100);
    the.map.drawHomeDistances();
    drawTerrainHeights();
    drawDefenseClusters();
    DistractorManager::Instance().drawDebug();

    // Najprv si uložíme výsledok funkcie do premennej
    BWAPI::TilePosition pos = the.bases.closestIslandExp();

    // Namiesto kontroly na nullptr použijeme funkciu isValid()
    if (pos.isValid())
    {
        // Konverzia TilePosition (mriežka) na Position (pixely) pre vykres¾ovanie
        BWAPI::Position worldPos = BWAPI::Position(pos);

        // Nakreslí azúrový kruh na mieste ostrova
        BWAPI::Broodwar->drawCircleMap(worldPos, 64, BWAPI::Colors::Cyan, false);
        //BWAPI::Broodwar->drawTextScreen(10, 100, "Target Island Base: %d, %d", pos.x, pos.y);

        // Napíše text priamo na mapu
        //BWAPI::Broodwar->drawTextMap(worldPos, "  Target Island Base");
    }
    else
    {
        // Ak funkcia vrátila neplatnú pozíciu (žiadny vo¾ný ostrov sa nenašiel)
        //BWAPI::Broodwar->drawTextScreen(10, 100, "Target Island Base: NOT FOUND");
    }
    
    _combatCommander.drawSquadInformation(170, 70);
    _timerManager.drawModuleTimers(490, 205);
    drawGameInformation(4, 1);

    drawUnitOrders();
    the.skillkit.draw();
}

void GameCommander::drawGameInformation(int x, int y)
{
    if (!Config::Debug::DrawGameInfo)
    {
        return;
    }

    const OpponentModel::OpponentSummary & summary = OpponentModel::Instance().getSummary();
    BWAPI::Broodwar->drawTextScreen(x, y, "%c%s %c%d-%d %c%s",
        BWAPI::Broodwar->self()->getTextColor(), BWAPI::Broodwar->self()->getName().c_str(),
        white, summary.totalWins, summary.totalGames - summary.totalWins,
        BWAPI::Broodwar->enemy()->getTextColor(), BWAPI::Broodwar->enemy()->getName().c_str());
    y += 12;
    
    const std::string & openingGroup = StrategyManager::Instance().getOpeningGroup();
    const auto openingInfoIt = summary.openingInfo.find(Config::Strategy::StrategyName);
    const int wins = openingInfoIt == summary.openingInfo.end() ? 0 : openingInfoIt->second.sameWins + openingInfoIt->second.otherWins;
    const int games = openingInfoIt == summary.openingInfo.end() ? 0 : openingInfoIt->second.sameGames + openingInfoIt->second.otherGames;
    bool gasSteal = ScoutManager::Instance().wantGasSteal();
    BWAPI::Broodwar->drawTextScreen(x, y, "\x03%s%s%s%s %c%d-%d",
        Config::Strategy::StrategyName.c_str(),
        openingGroup != "" ? (" (" + openingGroup + ")").c_str() : "",
        gasSteal ? " + steal gas" : "",
        Config::Strategy::FoundEnemySpecificStrategy ? " - enemy specific" : "",
        white, wins, games - wins);
    BWAPI::Broodwar->setTextSize();
    y += 12;

    std::string expect;
    std::string enemyPlanString;
    if (OpponentModel::Instance().getEnemyPlan() == OpeningPlan::Unknown &&
        OpponentModel::Instance().getExpectedEnemyPlan() != OpeningPlan::Unknown)
    {
        if (OpponentModel::Instance().isEnemySingleStrategy())
        {
            expect = "surely ";
        }
        else
        {
            expect = "expect ";
        }
        enemyPlanString = OpponentModel::Instance().getExpectedEnemyPlanString();
    }
    else
    {
        enemyPlanString = OpponentModel::Instance().getEnemyPlanString();
    }
    BWAPI::Broodwar->drawTextScreen(x, y, "%cOpp Plan %c%s%c%s", white, orange, expect.c_str(), yellow, enemyPlanString.c_str());
    y += 12;

    std::string island = "";
    if (Bases::Instance().isIslandStart())
    {
        island = " (island)";
    }
    BWAPI::Broodwar->drawTextScreen(x, y, "%c%s%c%s", yellow, BWAPI::Broodwar->mapFileName().c_str(), orange, island.c_str());
    BWAPI::Broodwar->setTextSize();
    y += 12;

    int frame = BWAPI::Broodwar->getFrameCount();
    BWAPI::Broodwar->drawTextScreen(x, y, "\x04%d %2u:%02u mean %.1fms max %.1fms",
        frame,
        int(frame / (23.8 * 60)),
        int(frame / 23.8) % 60,
        _timerManager.getMeanMilliseconds(),
        _timerManager.getMaxMilliseconds());

    /*
    // latency display
    y += 12;
    BWAPI::Broodwar->drawTextScreen(x + 50, y, "\x04%d max %d",
        BWAPI::Broodwar->getRemainingLatencyFrames(),
        BWAPI::Broodwar->getLatencyFrames());
    */
}

void GameCommander::drawUnitOrders()
{
    if (!Config::Debug::DrawUnitOrders)
    {
        return;
    }

    for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
    {
        if (!unit->getPosition().isValid())
        {
            continue;
        }

        std::string extra = "";
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Cocoon ||
            unit->getType().isBuilding() && !unit->isCompleted())
        {
            extra = UnitTypeName(unit->getBuildType());
        }
        else if (unit->isTraining() && !unit->getTrainingQueue().empty())
        {
            extra = UnitTypeName(unit->getTrainingQueue()[0]);
        }
        else if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
            unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
        {
            extra = UnitTypeName(unit);
        }
        else if (unit->isResearching())
        {
            extra = unit->getTech().getName();
        }
        else if (unit->isUpgrading())
        {
            extra = unit->getUpgrade().getName();
        }

        int x = unit->getPosition().x - 8;
        int y = unit->getPosition().y - 2;
        if (extra != "")
        {
            BWAPI::Broodwar->drawTextMap(x, y, "%c%s", yellow, extra.c_str());
        }
        if (unit->getOrder() != BWAPI::Orders::Nothing)
        {
            BWAPI::Broodwar->drawTextMap(x, y + 10, "%c%d %c%s", white, unit->getID(), cyan, unit->getOrder().getName().c_str());
        }
    }
}

void GameCommander::drawUnitCounts(int x, int y) const
{
    if (!Config::Debug::DrawUnitCounts)
    {
        return;
    }

    const int c1 = 17;
    const int c2 = 38;
    const int e0 = 160;
    int dy = 0;
    for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
    {
        if (the.my.all.count(t) > 0)
        {
            BWAPI::Broodwar->drawTextScreen    (x     , y + dy, "%c%3d" , white , the.my.completed.count(t));
            if (the.my.all.count(t) - the.my.completed.count(t) > 0)
            {
                BWAPI::Broodwar->drawTextScreen(x + c1, y + dy, "%c%+2d", yellow, the.my.all.count(t) - the.my.completed.count(t));
            }
            BWAPI::Broodwar->drawTextScreen    (x + c2, y + dy, "%c%s" , green , UnitTypeName(t).c_str());
            dy += 12;
        }
    }

    dy = 0;
    for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
    {
        if (the.your.seen.count(t) + the.your.inferred.count(t) > 0)
        {
            char color = red;
            int n = the.your.inferred.count(t);
            if (the.your.seen.count(t))
            {
                color = white;
                n = the.your.seen.count(t);
            }

            BWAPI::Broodwar->drawTextScreen    (x + e0          , y + dy, "%c%3d", color , n);
            BWAPI::Broodwar->drawTextScreen    (x + e0 + c2 - 13, y + dy, "%c%s" , orange, UnitTypeName(t).c_str());
            dy += 12;
        }
    }
}

void GameCommander::drawTerrainHeights() const
{
    if (!Config::Debug::DrawTerrainHeights)
    {
        return;
    }

    for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
    {
        for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
        {
            int h = BWAPI::Broodwar->getGroundHeight(x, y);
            char color = h % 2 ? purple : gray;

            BWAPI::Position pos(BWAPI::TilePosition(x, y));
            BWAPI::Broodwar->drawTextMap(pos + BWAPI::Position(12, 12), "%c%d", color, h);
        }
    }
}

void GameCommander::drawDefenseClusters()
{
    if (!Config::Debug::DrawDefenseClusters)
    {
        return;
    }

    const std::vector<UnitCluster> & groundClusters = the.ops.getGroundDefenseClusters();

    for (const UnitCluster & cluster : groundClusters)
    {
        cluster.draw(BWAPI::Colors::Brown, "vs ground");
    }

    const std::vector<UnitCluster> & airClusters = the.ops.getAirDefenseClusters();

    for (const UnitCluster & cluster : airClusters)
    {
        cluster.draw(BWAPI::Colors::Grey, "vs air");
    }
}

bool UAlbertaBot::GameCommander::shouldExpandRuleBased(std::vector<Record> records)
{
    std::string currentEnemyRace = BWAPI::Broodwar->enemy()->getRace().getName();
    std::string currentMap = BWAPI::Broodwar->mapName();

    // 1. FILTROVANIE POD¼A RASY
    std::vector<Record> filtered;
    if (!currentEnemyRace.empty()) {
        for (auto& r : records)
            if (r.race == currentEnemyRace)
                filtered.push_back(r);
    }
    if (filtered.empty())
        filtered = records;

    if (filtered.empty())
        return true;

    // --- NOVÁ ÈAS: WIN STREAK PROTECTION ---
    // Pozrieme sa na úplne posledný záznam po filtrovaní (najèerstvejší zápas)
    const Record& lastGame = filtered.back();

    // Ak sme naposledy expandovali A vyhrali sme, skúsime to znova bez oh¾adu na štatistiky
    if (lastGame.didExpand && lastGame.isWin) {
        return true;
    }
    // Volite¾ne: ak sme hrali 1-base a vyhrali sme, môžeme osta pri 1-base
    // else if (!lastGame.didExpand && lastGame.isWin) {
    //     return false;
    // }
    // ----------------------------------------

    // 2. DETEKCIA OPAKOVANIA STRATÉGIE BEZ VÝSLEDKU (Loss Streak)
    const int STREAK_CHECK = 3;
    if ((int)filtered.size() >= STREAK_CHECK) {
        auto begin = filtered.end() - STREAK_CHECK;
        bool recentExpand = begin->didExpand;
        bool allSameStrat = true;
        bool allLost = true;

        for (auto it = begin; it != filtered.end(); ++it) {
            if (it->didExpand != recentExpand) allSameStrat = false;
            if (it->isWin)                     allLost = false;
        }

        if (allSameStrat && allLost)
            return !recentExpand;   // Otoèíme taktiku, lebo 3x po sebe zlyhala
    }

    // 3. VÁŽENÉ SKÓRE
    double expandScore = 0.0, expandWeight = 0.0;
    double oneBaseScore = 0.0, oneBaseWeight = 0.0;

    for (auto& r : filtered) {
        double w = 1.0;
        if (r.map == currentMap) w *= 2.0;
        if (r.frame == -1)       w *= 0.5;

        if (r.didExpand) {
            expandWeight += w;
            if (r.isWin) expandScore += w;
        }
        else {
            oneBaseWeight += w;
            if (r.isWin) oneBaseScore += w;
        }
    }

    // 4. ROZHODOVANIE (Exploration & Win-rate)
    if (expandWeight == 0.0) return true;
    if (oneBaseWeight == 0.0) return false;

    double expandWinRate = expandScore / expandWeight;
    double oneBaseWinRate = oneBaseScore / oneBaseWeight;

    const double EXPAND_BIAS = 0.05;
    return (expandWinRate + EXPAND_BIAS) >= oneBaseWinRate;
}

bool GameCommander::shouldExpandML(const std::string opponentName)
{
    std::string currentMap = BWAPI::Broodwar->mapName();
    std::string enemyRace = BWAPI::Broodwar->enemy()->getRace().getName();

    // Získanie štatistík (potrebuješ mapStats upravi, aby vracala WR pre obe verzie)
    auto [wrEx, gamesEx] = getStrategyStats(currentMap, true);
    auto [wrNo, gamesNo] = getStrategyStats(currentMap, false);
    
    // Výpoèet váženého èasu útoku z histórie
    double avgAttack = calculateAvgAttackFrame(_history, enemyRace);

    // Vytvorenie dvoch scenárov pre model
    auto featExpand = buildFeatures(true, avgAttack, wrEx, wrNo, gamesEx + gamesNo);
    auto featNoExpand = buildFeatures(false, avgAttack, wrEx, wrNo, gamesEx + gamesNo);

    double pExpand = sigmoid(dotProduct(_weights, featExpand));
    double pNoExpand = sigmoid(dotProduct(_weights, featNoExpand));

    return pExpand > pNoExpand;
}

// assigns units to various managers
void GameCommander::handleUnitAssignments()
{
    _validUnits.clear();
    _combatUnits.clear();
    // Don't clear the scout units.

    // Only keep units which are completed and usable.
    setValidUnits();

    // set each type of unit
    setScoutUnits();
    setDistractorUnits();
    setTransportUnits();
    setCombatUnits();
}

bool GameCommander::isAssigned(BWAPI::Unit unit) const
{
    return _combatUnits.contains(unit) || _scoutUnits.contains(unit) || _distractorUnits.contains(unit)
           || _transportUnits.contains(unit);
}

// validates units as usable for distribution to various managers
void GameCommander::setValidUnits()
{
    /*
    // TODO testing
    std::string timingFile = Config::IO::WriteDir + "timing.csv";
    static bool speed = false;
    if (!speed && BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost) > 0)
    {
        BWAPI::Broodwar->printf("zergling speed %d", BWAPI::Broodwar->getFrameCount());
        Logger::LogAppendToFile(timingFile, "%d,%s\n", BWAPI::Broodwar->getFrameCount(), "speed");
        speed = true;
    }
    static BWAPI::Unitset lings;
    size_t nLings = lings.size();
    */

    for (BWAPI::Unit unit : BWAPI::Broodwar->self()->getUnits())
    {
        if (UnitUtil::IsValidUnit(unit))
        {	
            _validUnits.insert(unit);

            /*
            // TODO testing
            static bool firstTime1 = false;
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Lair && !firstTime1)
            {
                BWAPI::Broodwar->printf("lair timing %d", BWAPI::Broodwar->getFrameCount());
                firstTime1 = true;
            }
            // TODO testing
            //if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling)
            //{
            //    lings.insert(unit);
            //}
            */

            /*
            // TODO testing
            static bool firstTime1 = false;
            static BWAPI::Unitset reported;
            if (unit->getType() == BWAPI::UnitTypes::Protoss_Gateway && !reported.contains(unit))
            {
                BWAPI::Broodwar->printf("unit timing %d", BWAPI::Broodwar->getFrameCount());
                firstTime1 = true;
                reported.insert(unit);
            }
            */
        }
        else
        {
            /*
            static bool firstTime2 = false;
            if (!firstTime2 && unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && !unit->isCompleted())
            {
                BWAPI::Broodwar->printf("hatchery timing %d", BWAPI::Broodwar->getFrameCount());
                firstTime2 = true;
            }
            */
        }
    }

    /*
    // TODO testing
    if (lings.size() > nLings)
    {
        BWAPI::Broodwar->printf("%d lings @ %d", lings.size(), BWAPI::Broodwar->getFrameCount());
        Logger::LogAppendToFile(timingFile, "%d,%d,%d\n", BWAPI::Broodwar->getFrameCount(), lings.size(), BWAPI::Broodwar->self()->minerals());
    }
    */
}

void GameCommander::setScoutUnits()
{
    // If we're zerg, assign the first overlord to scout.
    if (BWAPI::Broodwar->getFrameCount() == 0 &&
        BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
    {
		for (BWAPI::Unit unit : BWAPI::Broodwar->self()->getUnits())
        {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
            {
                ScoutManager::Instance().setOverlordScout(unit);
                assignUnit(unit, _scoutUnits);
                break;
            }
        }
    }

    // Send a scout worker if we haven't yet and should.
    if (!_initialScoutTime)
    {
        if (ScoutManager::Instance().shouldScout() && !the.bases.isIslandStart())
        {
            BWAPI::Unit workerScout = getAnyFreeWorker();

            // If we find a worker, make it the scout unit.
            if (workerScout)
            {
                ScoutManager::Instance().setWorkerScout(workerScout);
                assignUnit(workerScout, _scoutUnits);
                _initialScoutTime = the.now();
            }
        }
    }
}

void GameCommander::setDistractorUnits()
{
    if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
    {
        if (!_initialDistractorTime)
        {
            if (DistractorManager::Instance().shouldDistract() && !the.bases.isIslandStart())
            {
                BWAPI::Unit zelgling = getAnyDistractor();

                if (zelgling)
                {
                    DistractorManager::Instance().assignUnit(zelgling);
                    assignUnit(zelgling, _distractorUnits);
                    _initialDistractorTime = the.now();
                }
            }
        }
    }
}

void GameCommander::setTransportUnits()
{
    // Ak TransportManager požiada o Overlorda, priradíme mu ho tu
    for (BWAPI::Unit unit : _validUnits)
    {
        if (TransportManager::Instance().needsTransportUnit(unit))
        {
            _transportUnits.insert(unit);
            TransportManager::Instance().addTransportUnit(unit);
        }
    }
}

// Set combat units to be passed to CombatCommander.
void GameCommander::setCombatUnits()
{
    for (BWAPI::Unit unit : _validUnits)
    {
        if (!isAssigned(unit) && (UnitUtil::IsCombatUnit(unit) || unit->getType().isWorker()))		
        {	
            assignUnit(unit, _combatUnits);
        }
    }
}

// Release the zerg scouting overlord for other duty.
void GameCommander::releaseOverlord(BWAPI::Unit overlord)
{
    _scoutUnits.erase(overlord);
}

void GameCommander::onUnitShow(BWAPI::Unit unit)			
{ 
    InformationManager::Instance().onUnitShow(unit); 
    WorkerManager::Instance().onUnitShow(unit);
}

void GameCommander::onUnitHide(BWAPI::Unit unit)			
{ 
    InformationManager::Instance().onUnitHide(unit); 
}

void GameCommander::onUnitCreate(BWAPI::Unit unit)		
{ 
    InformationManager::Instance().onUnitCreate(unit); 
	WorkerManager::Instance().onUnitDestroy(unit);
}

void GameCommander::onUnitComplete(BWAPI::Unit unit)
{
    InformationManager::Instance().onUnitComplete(unit);
}

void GameCommander::onUnitRenegade(BWAPI::Unit unit)		
{ 
    InformationManager::Instance().onUnitRenegade(unit); 
}

void GameCommander::onUnitDestroy(BWAPI::Unit unit)		
{ 	
    ProductionManager::Instance().onUnitDestroy(unit);
    WorkerManager::Instance().onUnitDestroy(unit);
    InformationManager::Instance().onUnitDestroy(unit); 
}

void GameCommander::onUnitMorph(BWAPI::Unit unit)		
{ 
    InformationManager::Instance().onUnitMorph(unit);
    WorkerManager::Instance().onUnitMorph(unit);
}

// Used only to choose a worker to scout.
BWAPI::Unit GameCommander::getAnyFreeWorker()
{
    for (BWAPI::Unit unit : _validUnits)
    {
        if (unit->getType().isWorker() &&
            !isAssigned(unit) &&
            WorkerManager::Instance().isFree(unit) &&
            !unit->isCarryingMinerals() &&
            !unit->isCarryingGas() &&
            unit->getOrder() != BWAPI::Orders::MiningMinerals)
        {
            return unit;
        }
    }

    return nullptr;
}

// Used only to choose a worker to scout.
BWAPI::Unit GameCommander::getAnyDistractor()
{
    for (BWAPI::Unit unit : _validUnits)
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
            !isAssigned(unit))
        {
            return unit;
        }
    }
    for (BWAPI::Unit unit : _validUnits)
    {
        if (unit->getType().isWorker() &&
            !isAssigned(unit))
        {
            return unit;
        }
    }
    return nullptr;
}

void GameCommander::assignUnit(BWAPI::Unit unit, BWAPI::Unitset & set)
{
    if (_scoutUnits.contains(unit)) { _scoutUnits.erase(unit); }
    else if (_combatUnits.contains(unit)) { _combatUnits.erase(unit); }

    set.insert(unit);
}

// Decide whether to give up early. See config option SurrenderWhenHopeIsLost.
// This depends on _validUnits, so call it after handleUnitAssignments().
// Giving up early is important in testing, to save time. In a serious tournament,
// it's a question of taste.
bool GameCommander::surrenderMonkey()
{
    if (!Config::Skills::SurrenderWhenHopeIsLost)
    {
        return false;
    }

    // Only check once every five seconds. No hurry to give up.
    if (BWAPI::Broodwar->getFrameCount() % (5 * 24) != 0)
    {
        return false;
    }

    // Don't surrender right at the start.
    if (BWAPI::Broodwar->getFrameCount() < 24 * 60)
    {
        return false;
    }

    // We are playing against a human. Surrender earlier to reduce frustration.
    if (Config::Skills::HumanOpponent)
    {
        int mySupply = BWAPI::Broodwar->self()->supplyUsed();
        PlayerSnapshot enemySnap(BWAPI::Broodwar->enemy());
        int knownEnemySupply = enemySnap.getSupply();

        // BWAPI::Broodwar->printf("supply %d < %d vs enemy %d", mySupply, _myHighWaterSupply, knownEnemySupply);

        if (mySupply > _myHighWaterSupply)
        {
            _myHighWaterSupply = mySupply;
            return false;
        }

        // Surrender if the enemy is way ahead AND we have been hurt.
        // We don't check that we were RECENTLY hurt.
        return mySupply < knownEnemySupply / 2 && mySupply < _myHighWaterSupply / 2;
    }

    // We assume we are playing against another bot.
    // The only reason to gg is to save time, so be conservative.
    // Surrender if all conditions are met:
    // 1. We don't have the cash to make a worker.
    // 2. We have no completed unit that can attack. (We may have incomplete units.)
    // 3. The enemy has at least one visible unit that can destroy buildings.
    // Terran does not float buildings, so we check whether the enemy can attack ground.

    // 1. Our cash.
    if (BWAPI::Broodwar->self()->minerals() >= 50)
    {
        return false;
    }

    // 2. Our units.
    for (BWAPI::Unit unit : _validUnits)
    {
        if (unit->canAttack())
        {
            return false;
        }
    }

    // 3. Enemy units.
    bool safe = true;
    for (BWAPI::Unit unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (unit->isVisible() && UnitUtil::CanAttackGround(unit))
        {
            safe = false;
            break;
        }
    }
    if (safe)
    {
        return false;
    }

    // Surrender monkey says surrender!
    return true;
}

GameCommander & GameCommander::Instance()
{
    static GameCommander instance;
    return instance;
}
