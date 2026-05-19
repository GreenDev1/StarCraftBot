#pragma once
#include "Common.h"
#include "BuildOrderQueue.h"
#include "BuildOrder.h"

using namespace UAlbertaBot;

struct TransportTask {
    BWAPI::Unit passenger;
    BWAPI::Position destination;
    BWAPI::Unit transportUnit; // Overlord
    int timeRequested;
    std::string state; // "WAITING_FOR_TECH", "WAITING_FOR_TRANSPORT", "LOADING", "TRANSIT", "UNLOADING"

};

// Enum PRED triedou – môžeš ho použiť aj inde ak treba
enum class TechPhaseT {
    IDLE,
    POOL_REQUESTED,
    POOL_IN_PROGRESS,
    POOL_DONE,
    LAIR_REQUESTED,
    LAIR_IN_PROGRESS,
    LAIR_DONE,
    VENTRAL_REQUESTED,
    VENTRAL_DONE
};

class TransportManager
{
    TransportManager();
    std::vector<TransportTask> _tasks;
    BWAPI::Unitset _transports;
    void handleTech(TransportTask& task, BuildOrderQueue & queue);

    TechPhaseT _techPhaseT = TechPhaseT::IDLE;
    int _phaseStartFrameT = 0;

public:
    static TransportManager & Instance();
    
    void update(BuildOrderQueue & queue);
    void requestTransport(BWAPI::Unit passenger, BWAPI::Position destination);
    
    bool needsTransportUnit(BWAPI::Unit unit);
    void addTransportUnit(BWAPI::Unit unit);
    
    // Zistí, či je pasažier aktuálne v správe transportu
    bool isBeingTransported(BWAPI::Unit passenger);
    BWAPI::Unit findBestOverlord(BWAPI::Unit passenger, BWAPI::Position destination);

};