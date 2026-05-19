#pragma once

#include "BWAPI.h"
#include <vector>
#include "MacroCommand.h"
#include <unordered_map>
#include "GridDistances.h"


// DistractorManager
// -----------------
// Controls a single unit that loiters at the best intercept choke point
// between our base and all potential enemy start locations.
//
// Uses steamhammer's GridDistances BFS maps to trace terrain-legal paths
// from each enemy start toward our base, then finds the choke tile that:
//   1. Appears on the most enemy approach paths (maximum coverage)
//   2. Is outside our base vision radius
//   3. Among ties, is furthest from our base (intercepts earliest)
//
// Call drawDebug() each frame alongside other debug draws.

namespace UAlbertaBot
{
    class DistractorManager
    {
        BWAPI::Unit _unit;                   // The unit used as distractor
        BWAPI::TilePosition _targetTile;     // Position to move to
        MacroCommandType	_command;

        // Private constructor so no other instance can be made
        DistractorManager();

        // One per potential enemy start: the gradient-traced terrain path
        // from that start toward our base (sampled tiles, not every tile).
        // One per potential enemy start: the gradient-traced terrain path
        // from that start toward our base (sampled tiles, not every tile).
        struct ApproachPath
        {
            BWAPI::Position                  enemyStart;
            std::vector<BWAPI::TilePosition> pathTiles;    // terrain-legal, enemy→us
            BWAPI::TilePosition              watchHit;     // first choke found on this path outside vision
        };
        std::vector<ApproachPath> _approachPaths;

        // Gradient-walk from startTile toward our base using a pre-built distance grid.
        // Returns the sequence of sampled tiles (every kSampleStep steps).
        std::vector<BWAPI::TilePosition> tracePath(
            const BWAPI::TilePosition& startTile,
            const class GridDistances& distFromBase) const;

        // Returns true if tile is NOT within vision range of any of OUR bases.
        bool outsideBaseVision(BWAPI::TilePosition tile, int visionPixels) const;

        BWAPI::Position calculateLurePosition(const std::vector<BWAPI::Unit>& enemies);

        // Replace BWAPI::TilePosition _targetTile; with:
        std::vector<BWAPI::TilePosition> _patrolWaypoints;
        size_t _currentWaypointIndex = 0;

        // Update the return type of the calculation function:
        std::vector<BWAPI::TilePosition> findInterceptPoints();

        static constexpr int kWorkerThreatRadius = 1100;   // polomer sledovania nepriatela pri základni
        static constexpr int kWorkerLeashRadius = 1300;   // ak sa vzdiali ďalej, pustíme ho

        // Člen triedy
        BWAPI::Unit _targetWorker = nullptr;   // sledovaný nepriateľský worker

    public:
        // Delete copy constructor and assignment operator
        DistractorManager(const DistractorManager&) = delete;
        DistractorManager& operator=(const DistractorManager&) = delete;

        // Singleton accessor
        static DistractorManager& Instance()
        {
            static DistractorManager instance;
            return instance;
        }

        void assignUnit(BWAPI::Unit unit);
        void update();                       // Call every frame
        void setCommand(MacroCommandType cmd);
        bool shouldDistract();
        void drawDebug() const;
    };
}
