#include "TransportManager.h"
#include "MacroAct.h"
#include "BuildOrderQueue.h"
#include "MacroAct.h"
#include <array>
#include "The.h"
#include "BuildingManager.h"

using namespace UAlbertaBot;


TransportManager & TransportManager::Instance()
{
    static TransportManager instance;
    return instance;
}

TransportManager::TransportManager() {}

void TransportManager::update(BuildOrderQueue & queue)
{
    for (auto it = _tasks.begin(); it != _tasks.end(); )
    {
        TransportTask& task = *it;

        // POISTKA 1: Pasažier zomrel alebo prestal existovať
        if (!task.passenger || !task.passenger->exists() || task.passenger->getHitPoints() <= 0)
        {
            // Ak už bol vnútri Overlorda, Overlord mohol zomrieť s ním
            it = _tasks.erase(it);
            continue;
        }

        // POISTKA 2: Overlord zomrel počas nakladania alebo letu
        if (task.transportUnit && (!task.transportUnit->exists() || task.transportUnit->getHitPoints() <= 0))
        {
            // Ak bol Dron už naložený, zomrel spolu s Overlordom (zachytené v Poistke 1)
            // Ak nebol, musíme len zohnať nového Overlorda
            if (task.state == "LOADING" || task.state == "TRANSIT") {
                task.transportUnit = nullptr;
                task.state = "WAITING_FOR_TRANSPORT";
            }
        }

        // STAV: ČAKANIE NA TECH
        if (task.state == "WAITING_FOR_TECH") {
            handleTech(task, queue);
        }

        // STAV: HĽADANIE DOPRAVY
        if (task.state == "WAITING_FOR_TRANSPORT") {
            task.transportUnit = findBestOverlord(task.passenger, task.destination);
            if (task.transportUnit) {
                task.state = "LOADING";
            }
        }

        // STAV: NAKLADANIE
        if (task.state == "LOADING") {
            if (!task.transportUnit || !task.transportUnit->exists()) {
                task.state = "WAITING_FOR_TRANSPORT"; // Overlord zomrel, hľadaj iného
                continue;
            }

            if (task.passenger->isLoaded()) {
                task.state = "TRANSIT";
            }
            else {
                // Mikro-manažment naloženia
                if (task.transportUnit->getDistance(task.passenger) > 10) {
                    task.transportUnit->move(task.passenger->getPosition());
                    task.passenger->move(task.transportUnit->getPosition());
                }
                else {
                    task.transportUnit->load(task.passenger);
                }
            }
        }

        // STAV: CESTA (TRANSIT)
        if (task.state == "TRANSIT") {
            if (task.transportUnit->getDistance(task.destination) > 160) {
                task.transportUnit->move(task.destination);
            }
            else {
                task.state = "APPROACHING_UNLOAD";
            }
        }
        // VŠETKY OSTATNÉ STAVY MUSIA BYŤ else if !
        else if (task.state == "APPROACHING_UNLOAD") {
            BWAPI::TilePosition centerTile(task.destination);
            BWAPI::TilePosition safeTile = BWAPI::TilePositions::None;

            for (int x = -3; x <= 3 && safeTile == BWAPI::TilePositions::None; x++) {
                for (int y = -3; y <= 3; y++) {
                    BWAPI::TilePosition tp(centerTile.x + x, centerTile.y + y);
                    // canBuildHere zlyháva na ostrovoch - použi len isWalkable
                    if (tp.isValid() && BWAPI::Broodwar->isWalkable(tp.x * 4, tp.y * 4)) {
                        safeTile = tp;
                        break;
                    }
                }
            }

            BWAPI::Position safePos = (safeTile != BWAPI::TilePositions::None)
                ? BWAPI::Position(safeTile)
                : task.destination;

            if (task.transportUnit->getDistance(safePos) > 32) {
                task.transportUnit->move(safePos);
            }
            else {
                task.state = "UNLOADING";
            }
        }
        else if (task.state == "UNLOADING") {
            if (task.transportUnit->getLoadedUnits().empty()) {
                task.state = "FINISHED";
            }
            else {
                // Priamy BWAPI príkaz – obíde Steamhammerov mikro systém
                // Unload každých 12 framov (nie každý frame) aby príkaz stihol
                if (BWAPI::Broodwar->getFrameCount() % 12 == 0) {
                    task.transportUnit->unloadAll(task.transportUnit->getPosition());
                }

                BWAPI::Broodwar->drawCircleMap(
                    task.transportUnit->getPosition(), 15, BWAPI::Colors::Red, true);
                BWAPI::Broodwar->drawTextMap(
                    task.transportUnit->getPosition(), "UNLOADING...");
            }
        }

        ++it;
    }

    // Vyčistenie hotových úloh
    _tasks.erase(std::remove_if(_tasks.begin(), _tasks.end(),
        [](const TransportTask& t) { return t.state == "FINISHED"; }), _tasks.end());
}

void TransportManager::requestTransport(BWAPI::Unit passenger, BWAPI::Position destination)
{
    // Skontrolujeme, či už požiadavku nemá
    for (auto & task : _tasks) {
        if (task.passenger == passenger) return; 
    }

    TransportTask newTask;
    newTask.passenger = passenger;
    newTask.destination = destination;
    newTask.transportUnit = nullptr;
    newTask.timeRequested = BWAPI::Broodwar->getFrameCount();
    newTask.state = "WAITING_FOR_TECH"; // Predvolený stav

    _tasks.push_back(newTask);
    BWAPI::Broodwar->printf("nova uloha");
}

bool TransportManager::needsTransportUnit(BWAPI::Unit unit)
{
    return false;
}

void TransportManager::addTransportUnit(BWAPI::Unit unit)
{
}

bool TransportManager::isBeingTransported(BWAPI::Unit passenger)
{
    for (const auto& task : _tasks)
    {
        if (task.passenger == passenger)
        {
            // Transportovaný = kýkoľvek kým úloha existuje a nie je FINISHED
            return task.state != "FINISHED";
        }
    }
    return false;
}

bool isAlreadyInProject(BWAPI::UnitType type, BuildOrderQueue& queue) {
    auto self = BWAPI::Broodwar->self();

    // 1. Kontrola BWAPI
    int bwapiCount = self->allUnitCount(type);
    if (bwapiCount > 0) {
         BWAPI::Broodwar->printf("Debug: %s najdeny v BWAPI (%d)", type.getName().c_str(), bwapiCount);
        return true;
    }

    // 2. Kontrola BuildingManagera
    int unstarted = UAlbertaBot::BuildingManager::Instance().getNumUnstarted(type);
    bool beingBuilt = UAlbertaBot::BuildingManager::Instance().isBeingBuilt(type);
    if (unstarted > 0 || beingBuilt) {
        BWAPI::Broodwar->printf("Debug: %s v BuildingMgr (Unst:%d, Built:%d)", type.getName().c_str(), unstarted, beingBuilt);
        return true;
    }

    // 3. Kontrola Queue
    for (int i = 0; i < (int)queue.size(); ++i) {
        if (queue[i].macroAct.isUnit() && queue[i].macroAct.getUnitType() == type) {
             BWAPI::Broodwar->printf("Debug: %s najdeny v Queue na pozicii %d", type.getName().c_str(), i);
            return true;
        }
    }

    return false;
}

bool isBeingBuiltOrPlanned(BWAPI::UnitType type) {
    auto self = BWAPI::Broodwar->self();

    // 1. BWAPI už o tom vie (Dron mutuje)
    if (self->incompleteUnitCount(type) > 0) return true;

    // 2. Steamhammer o tom vie, ale Dron ešte len cestuje k lokácii
    // Predpokladám prístup k BuildingManager::Instance()
    if (BuildingManager::Instance().getNumUnstarted(type) > 0) return true;

    return false;
}

// Pomocná funkcia s robustným hľadaním
bool safePrioritize(BuildOrderQueue& queue, BWAPI::UnitType type) {
    int foundIndex = -1;

    // 1. Hľadáme PRVÝ výskyt vo fronte
    for (int i = (int)queue.size() - 1; i >= 0; --i) {
        if (queue[i].macroAct.isUnit() && queue[i].macroAct.getUnitType() == type) {
            foundIndex = i;
            break; // Našli sme prvý, končíme hľadanie
        }
    }

    if (foundIndex != -1) {
        // Ak je v queue, presunieme ho na začiatok len ak tam už nie je
        if (foundIndex > 0) {
            queue.pullToTop(foundIndex);
            return true; // Zmena vykonaná
        }
        return false; // Už je na nule, nič netreba robiť
    }
    else {
        // Ak v queue nie je vôbec, až vtedy pridáme jeden nový na začiatok
        queue.queueAsHighestPriority(MacroAct(type));
        return true;
    }
}

// To isté pre Upgrady
bool safePrioritize(BuildOrderQueue& queue, BWAPI::UpgradeType type) {
    int foundIndex = -1;
    for (int i = (int)queue.size() - 1; i >= 0; --i) {
        if (queue[i].macroAct.isUpgrade() && queue[i].macroAct.getUpgradeType() == type) {
            foundIndex = i;
            break;
        }
    }
    if (foundIndex != -1) {
        if (foundIndex > 0) {
            queue.pullToTop(foundIndex);
            return true;
        }
        return false;
    }
    else {
        queue.queueAsHighestPriority(MacroAct(type));
        return true;
    }
}

void TransportManager::handleTech(TransportTask& task, BuildOrderQueue& queue)
{
    auto self = BWAPI::Broodwar->self();
    int frame = BWAPI::Broodwar->getFrameCount();

    // ============================================================
    // FÁZA 1: SPAWNING POOL
    // ============================================================
    if (self->completedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0)
    {
        // BWAPI hovorí, že Pool existuje fyzicky (stavia sa alebo hotový)
        bool physicallyExists =
            self->incompleteUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 ||
            self->completedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;

        // BuildingManager vie o ňom (Dron cestuje alebo čaká)
        bool managerKnows =
            UAlbertaBot::BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) ||
            UAlbertaBot::BuildingManager::Instance().getNumUnstarted(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;

        // --- Prechody stavu ---

        if (_techPhaseT == TechPhaseT::IDLE)
        {
            // Prvýkrát – pridáme do queue a ZAZNAMENÁME stav
            BWAPI::Broodwar->printf("[Tech] Faza IDLE -> POOL_REQUESTED. Pridavam Pool.");
            safePrioritize(queue, BWAPI::UnitTypes::Zerg_Spawning_Pool);
            _techPhaseT = TechPhaseT::POOL_REQUESTED;
            _phaseStartFrameT = frame;
            return;
        }

        if (_techPhaseT == TechPhaseT::POOL_REQUESTED)
        {
            // Čakáme, kým BuildingManager alebo BWAPI potvrdia, že Dron dostal úlohu.
            // Dávame mu max 30 sekúnd (= 720 framov pri 24fps), potom retry.
            if (physicallyExists || managerKnows)
            {
                BWAPI::Broodwar->printf("[Tech] POOL_REQUESTED -> POOL_IN_PROGRESS. Potvrdzujem.");
                _techPhaseT = TechPhaseT::POOL_IN_PROGRESS;
                return;
            }

            // Timeout ochrana: ak po 720 framoch stale nič, retry (ale iba raz!)
            if (frame - _phaseStartFrameT > 720)
            {
                BWAPI::Broodwar->printf("[Tech] POOL_REQUESTED timeout! Retry.");
                // Vymažeme prípadné duplikáty a pridáme čistú položku
                //removeAllFromQueue(queue, BWAPI::UnitTypes::Zerg_Spawning_Pool); // viz nižšie
                safePrioritize(queue, BWAPI::UnitTypes::Zerg_Spawning_Pool);
                _phaseStartFrameT = frame; // Reset timeoutu
            }
            return; // V tejto fáze NIKDY nepridávame ďalší Pool
        }

        if (_techPhaseT == TechPhaseT::POOL_IN_PROGRESS)
        {
            // Len čakáme na dokončenie. NIČ nerobia s queue.
            return;
        }

        return; // Sme v inej fáze, Pool riešia iné vetvy
    }

    // Pool je hotový, posunieme fázu
    if (_techPhaseT < TechPhaseT::POOL_DONE)
    {
        BWAPI::Broodwar->printf("[Tech] Pool DONE.");
        _techPhaseT = TechPhaseT::POOL_DONE;
    }

    // ============================================================
    // FÁZA 2: LAIR
    // ============================================================
    if (_techPhaseT == TechPhaseT::POOL_DONE || _techPhaseT == TechPhaseT::LAIR_REQUESTED)
    {
        bool lairExists =
            self->completedUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0 ||
            self->incompleteUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0 ||
            UAlbertaBot::BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Lair);

        if (!lairExists)
        {
            if (self->gas() < 95) return;

            if (_techPhaseT == TechPhaseT::POOL_DONE) // Pridáme iba raz!
            {
                BWAPI::Broodwar->printf("[Tech] POOL_DONE -> LAIR_REQUESTED.");
                safePrioritize(queue, BWAPI::UnitTypes::Zerg_Lair);
                _techPhaseT = TechPhaseT::LAIR_REQUESTED;
                _phaseStartFrameT = frame;
            }
            // Ak sme LAIR_REQUESTED, len čakáme – nepridávame znova!
            return;
        }

        _techPhaseT = TechPhaseT::LAIR_IN_PROGRESS;
    }

    if (_techPhaseT == TechPhaseT::LAIR_IN_PROGRESS)
    {
        if (self->completedUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0) return;
        _techPhaseT = TechPhaseT::LAIR_DONE;
        BWAPI::Broodwar->printf("[Tech] Lair DONE.");
    }

    // ============================================================
    // FÁZA 3: VENTRAL SACS
    // ============================================================
    if (_techPhaseT == TechPhaseT::LAIR_DONE || _techPhaseT == TechPhaseT::VENTRAL_REQUESTED)
    {
        int level = self->getUpgradeLevel(BWAPI::UpgradeTypes::Ventral_Sacs);
        bool upgrading = self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs);

        if (level == 0 && !upgrading)
        {
            if (self->gas() < 190) return;

            if (_techPhaseT == TechPhaseT::LAIR_DONE) // Pridáme iba raz!
            {
                safePrioritize(queue, BWAPI::UpgradeTypes::Ventral_Sacs);
                _techPhaseT = TechPhaseT::VENTRAL_REQUESTED;
                BWAPI::Broodwar->printf("[Tech] LAIR_DONE -> VENTRAL_REQUESTED.");
            }
            return;
        }

        if (level > 0)
        {
            _techPhaseT = TechPhaseT::VENTRAL_DONE;
            task.state = "WAITING_FOR_TRANSPORT";
        }
    }
}

BWAPI::Unit TransportManager::findBestOverlord(BWAPI::Unit passenger, BWAPI::Position destination)
{
    BWAPI::Unit bestOverlord = nullptr;
    double minDistance = std::numeric_limits<double>::max();

    BWAPI::Position passengerPos = passenger->getPosition();

    for (auto& ovie : BWAPI::Broodwar->self()->getUnits())
    {
        if (ovie->getType() != BWAPI::UnitTypes::Zerg_Overlord || !ovie->isCompleted()) continue;

        // Ak je už ovie pridelený inej transporte úlohe
        bool isBusy = false;
        BWAPI::Position currentDest;
        for (auto& t : _tasks) {
            if (t.transportUnit == ovie) {
                isBusy = true;
                currentDest = t.destination;
                break;
            }
        }

        double distToPassenger = ovie->getDistance(passengerPos);

        if (!isBusy) {
            if (distToPassenger < minDistance) {
                minDistance = distToPassenger;
                bestOverlord = ovie;
            }
        }
        else {
            // Tvoja podmienka: Ak je obsadený, ale zachádzka je < 15%
            // Celková nová trasa: Ovie -> Pasažier -> Cieľ pasažiera -> Pôvodný cieľ
            double originalDist = ovie->getDistance(currentDest);
            double newDist =
                ovie->getDistance(passengerPos) +           // ide k pasažierovi
                passengerPos.getDistance(destination) +     // ide na cieľ
                destination.getDistance(currentDest);       // pokračuje ďalej

            if (newDist <= originalDist * 1.15) {
                if (distToPassenger < minDistance) {
                    minDistance = distToPassenger;
                    bestOverlord = ovie;
                }
            }
        }
    }
    return bestOverlord;
}