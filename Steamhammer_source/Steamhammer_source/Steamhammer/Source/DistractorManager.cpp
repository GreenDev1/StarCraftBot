#include "DistractorManager.h"
#include "Bases.h"
#include "Base.h"
#include "Config.h"
#include "The.h"
#include "GridDistances.h"

using namespace UAlbertaBot;

namespace
{
    // How far from every friendly base the unit must stay to remain invisible.
    // Standard base structure sight = 7 tiles = 224 px. We pad by 3 tiles.
    constexpr int kBaseVisionPixels = 10 * 32;   // 320 px  (10 tiles)

    // tileRoom value at or below which a tile is considered a choke.
    // Must match GridZone::chokeWidth = 12.
    constexpr int kChokeRoom = 12;

    // Minimum BFS distance (tiles) from our base the intercept may sit.
    constexpr int kMinDistFromBase = 15;

    // Maximum BFS distance (tiles) from our base — keeps the unit from being
    // placed uselessly deep in enemy territory.
    constexpr int kMaxDistFromBase = 28;

    // "Watch radius" in tiles: how far a unit standing at a candidate tile
    // can observe an approaching path tile.  Roughly matches unit sight range.
    constexpr int kWatchRadius = 6;

    // Gradient trace: step this many tiles at a time along the path.
    // Smaller = smoother debug line, but more work. 2 is fine.
    constexpr int kTraceStep = 2;

    // How often (frames) to recompute (in case enemy base gets confirmed/ruled out).
    constexpr int kRecomputeInterval = 300;

    // Stop re-issuing move when closer than this (pixels).
    constexpr int kArrivalRadius = 15;

    // Distance at which enemies trigger the unit to flee
    constexpr int kEnemyThreatRadius = 8 * 32;

    // How far ahead to look when picking a flee direction
    constexpr int kLureStepPixels = 13 * 32;

    // Debug colour for luring
    const BWAPI::Color kLureColour = BWAPI::Colors::Red;

    // Debug colours
    const BWAPI::Color kInterceptColour = BWAPI::Colors::Cyan;
    const BWAPI::Color kPathColour = BWAPI::Colors::Orange;
    const BWAPI::Color kWatchHitColour = BWAPI::Colors::Yellow;
    const BWAPI::Color kVisionRingColour = BWAPI::Colors::Purple;
    const BWAPI::Color kEnemyDotColour = BWAPI::Colors::Red;
}

DistractorManager::DistractorManager()
    : _unit(nullptr)
    , _targetTile(BWAPI::TilePositions::Invalid)
{
}

void DistractorManager::assignUnit(BWAPI::Unit unit)
{
    _unit = unit;
    // Find intercept point as soon as unit is assigned
    if (!_unit || !_unit->exists())
        return;

    if (_targetTile.isValid())
        BWAPI::Broodwar->printf("[Distractor] intercept tile (%d,%d)",
            _targetTile.x, _targetTile.y);
    else
        BWAPI::Broodwar->printf("[Distractor] no intercept found");
}

void DistractorManager::update()
{
    if (!_unit || !_unit->exists())
        return;

    // ??????????????????????????????????????????????????
    // PRIORITA 0: Obrana základne pred nepriate¾ským workerom
    // ??????????????????????????????????????????????????

    // Základòa = štartovacia pozícia nášho hráèa
    BWAPI::Position basePos(BWAPI::Broodwar->self()->getStartLocation());

    // Ak sledujeme workera, skontroluj èi stále existuje a èi je stále blízko základne
    if (_targetWorker && _targetWorker->exists())
    {
        if (_targetWorker->getDistance(basePos) > kWorkerLeashRadius)
        {
            // Vzdialil sa – prestaneme ho sledova, vrátime sa do normálneho režimu
            _targetWorker = nullptr;
        }
        else
        {
            // Stále je blízko – útoèíme naòho
            if (_unit->getOrderTarget() != _targetWorker)
                _unit->attack(_targetWorker);

            BWAPI::Broodwar->drawLineMap(
                _unit->getPosition(),
                _targetWorker->getPosition(),
                BWAPI::Colors::Red
            );
            return;   // ? preskoèíme lure/intercept logiku
        }
    }

    // Ak momentálne nemáme cie¾, h¾adáme nového nepriate¾ského workera pri základni
    if (!_targetWorker)
    {
        BWAPI::Unit closestWorker = nullptr;
        double      bestDist = std::numeric_limits<double>::max();

        for (BWAPI::Unit u : BWAPI::Broodwar->getUnitsInRadius(
            basePos, kWorkerThreatRadius,
            BWAPI::Filter::IsEnemy&& BWAPI::Filter::IsWorker))
        {
            double d = u->getDistance(basePos);
            if (d < bestDist)
            {
                bestDist = d;
                closestWorker = u;
            }
        }

        if (closestWorker)
        {
            _targetWorker = closestWorker;
            _unit->attack(_targetWorker);   // okamžite zaèneme útok

            BWAPI::Broodwar->drawLineMap(
                _unit->getPosition(),
                _targetWorker->getPosition(),
                BWAPI::Colors::Red
            );
            return;
        }
    }

    // ??????????????????????????????????????????????????
    // 1. Check for nearby threatening enemies (pôvodný kód)
    // ??????????????????????????????????????????????????
    std::vector<BWAPI::Unit> threateningEnemies;
    for (BWAPI::Unit u : BWAPI::Broodwar->getUnitsInRadius(
        _unit->getPosition(), kEnemyThreatRadius, BWAPI::Filter::IsEnemy))
    {
        if (!u->getType().isWorker() && !u->getType().isBuilding())
            threateningEnemies.push_back(u);
    }

    // 2. State Machine: Lure vs Intercept (pôvodný kód, nezmenený)
    if (!threateningEnemies.empty())
    {
        BWAPI::Position lurePos = calculateLurePosition(threateningEnemies);
        if (lurePos.isValid())
        {
            _unit->move(lurePos);
            BWAPI::Broodwar->drawLineMap(_unit->getPosition(), lurePos, kLureColour);
        }
    }
    else
    {
        if (_patrolWaypoints.empty() || (BWAPI::Broodwar->getFrameCount() % kRecomputeInterval == 1))
        {
            _patrolWaypoints = findInterceptPoints();
            _currentWaypointIndex = 0;
        }
        if (_patrolWaypoints.empty())
            return;

        if (_currentWaypointIndex >= _patrolWaypoints.size())
            _currentWaypointIndex = 0;

        BWAPI::Position targetPos(_patrolWaypoints[_currentWaypointIndex]);

        if (_unit->getDistance(targetPos) > kArrivalRadius)
            _unit->move(targetPos);
        else
            _currentWaypointIndex = (_currentWaypointIndex + 1) % _patrolWaypoints.size();
    }
}

void DistractorManager::drawDebug() const
{
    // Draw each terrain-traced path as a polyline, and highlight its choke hit.
    for (const ApproachPath& ap : _approachPaths)
    {
        // Red dot at enemy start
        BWAPI::Broodwar->drawCircleMap(ap.enemyStart, 6, kEnemyDotColour, true);

        // Orange polyline through sampled path tiles
        for (int i = 1; i < (int)ap.pathTiles.size(); ++i)
        {
            BWAPI::Position a(ap.pathTiles[i - 1]);
            BWAPI::Position b(ap.pathTiles[i]);
            BWAPI::Broodwar->drawLineMap(a, b, kPathColour);
        }

        if (ap.watchHit.isValid())
            BWAPI::Broodwar->drawCircleMap(BWAPI::Position(ap.watchHit),6, kWatchHitColour, false);
    }

    // Cyan filled circle + ring at the chosen intercept tile
    if (_targetTile.isValid())
    {
        BWAPI::Position iPos(_targetTile);
        // Watch-radius ring — visualises what the unit can observe from here
        BWAPI::Broodwar->drawCircleMap(iPos, kWatchRadius * 32, kInterceptColour, false);

        BWAPI::Broodwar->drawCircleMap(iPos, 10, kInterceptColour, true);
        BWAPI::Broodwar->drawCircleMap(iPos, 14, kInterceptColour, false);
        BWAPI::Broodwar->drawTextMap(iPos + BWAPI::Position(16, -8), "\x1F Intercept");
    }

    // Purple ring = vision exclusion around our base
    Base* myBase = the.bases.myNatural();
    if (!myBase || the.self() != myBase->getOwner())
        myBase = the.bases.myMain();
    if (myBase)
    {
        BWAPI::Broodwar->drawCircleMap(myBase->getCenter(),
            kBaseVisionPixels, kVisionRingColour, false);
    }

    if (_unit && _unit->exists())
    {
        BWAPI::Broodwar->drawCircleMap(_unit->getPosition(), kEnemyThreatRadius, BWAPI::Colors::Red, false);
    }

    // Draw Patrol Route Waypoints and connecting lines
    if (!_patrolWaypoints.empty())
    {
        for (size_t i = 0; i < _patrolWaypoints.size(); ++i)
        {
            BWAPI::Position p1(_patrolWaypoints[i]);
            // Get the next waypoint to draw a line to it (loops back to 0 at the end)
            BWAPI::Position p2(_patrolWaypoints[(i + 1) % _patrolWaypoints.size()]);

            // Draw the waypoint circles
            BWAPI::Broodwar->drawCircleMap(p1, kWatchRadius * 32, kInterceptColour, false);
            BWAPI::Broodwar->drawCircleMap(p1, 10, kInterceptColour, true);
            BWAPI::Broodwar->drawCircleMap(p1, 14, kInterceptColour, false);

            // Label the waypoints 0, 1, 2, etc.
            BWAPI::Broodwar->drawTextMap(p1 + BWAPI::Position(16, -8), "\x1F Patrol %d", i);

            // Connect the waypoints with a green line
            if (_patrolWaypoints.size() > 1)
            {
                BWAPI::Broodwar->drawLineMap(p1, p2, BWAPI::Colors::Green);
            }
        }

        // Draw a white line from the unit to its CURRENT target waypoint
        if (_unit && _unit->exists() && _currentWaypointIndex < _patrolWaypoints.size())
        {
            BWAPI::Position currentTarget(_patrolWaypoints[_currentWaypointIndex]);
            BWAPI::Broodwar->drawLineMap(_unit->getPosition(), currentTarget, BWAPI::Colors::White);
        }
    }
}

std::vector<BWAPI::TilePosition> DistractorManager::findInterceptPoints()
//BWAPI::TilePosition DistractorManager::findInterceptPoint()
{
    _approachPaths.clear();

    // --- 1. Get o
    // ur reference base -------------------------------------------
    Base* myBase = the.bases.myNatural();
    if (!myBase || the.self() != myBase->getOwner())
        myBase = the.bases.myMain();
    if (!myBase)
        return {};

    const BWAPI::TilePosition myTile = myBase->getTilePosition();

    // --- 2. Build BFS distance map from our base -------------------------
    // neutralBlocks=false so minerals / buildings don't block the trace.
    // This fills every reachable tile with its ground distance to myTile.
    GridDistances distFromBase(myTile, /* neutralBlocks = */ false);

    // --- 3. Collect potential enemy start locations -----------------------
    std::vector<BWAPI::TilePosition> enemyStarts;

    // Check if we already know the enemy's starting base
    Base* knownEnemyStart = the.bases.enemyStart();
    if (knownEnemyStart != nullptr)
    {
        // We know exactly where the enemy is, so just use that location
        enemyStarts.push_back(knownEnemyStart->getTilePosition());
    }
    else
    {
        // Enemy base is unknown: collect all valid start locations
        for (const BWAPI::TilePosition& sl : BWAPI::Broodwar->getStartLocations())
        {
            if (sl == myTile) continue;

            Base* b = the.bases.getBaseAtTilePosition(sl);
            if (b && b->getOwner() == the.self()) continue;

            // Rule out: explored tile with no enemy buildings visible.
            if (BWAPI::Broodwar->isExplored(sl))
            {
                bool hasEnemyBuilding = false;
                for (BWAPI::Unit u : BWAPI::Broodwar->getUnitsInRadius(
                    BWAPI::Position(sl), 12 * 32,
                    BWAPI::Filter::IsBuilding&& BWAPI::Filter::IsEnemy))
                {
                    (void)u;
                    hasEnemyBuilding = true;
                    break;
                }
                if (!hasEnemyBuilding) continue;
            }

            enemyStarts.push_back(sl);
        }
    }

    if (enemyStarts.empty())
        return {};

    // --- 4. Trace all approach paths --------------------------------------
    for (const BWAPI::TilePosition& startTile : enemyStarts)
    {
        ApproachPath ap;
        ap.enemyStart = BWAPI::Position(startTile) + BWAPI::Position(64, 48);
        ap.watchHit = BWAPI::TilePositions::Invalid;
        ap.pathTiles = tracePath(startTile, distFromBase);
        _approachPaths.push_back(std::move(ap));
    }

    // --- 5 & 6. Find the best waypoint for EACH approach path --------------
    std::vector<BWAPI::TilePosition> patrolPoints;

    for (const ApproachPath& ap : _approachPaths)
    {
        BWAPI::TilePosition bestForPath = BWAPI::TilePositions::Invalid;
        int maxDist = -1;

        // Find the tile on THIS specific path that is furthest from our base
        // while still respecting our safety distances and walkability.
        for (const BWAPI::TilePosition& tile : ap.pathTiles)
        {
            const int dist = distFromBase.at(tile);

            if (dist < kMinDistFromBase || dist > kMaxDistFromBase) continue;
            if (!the.map.isTerrainWalkable(tile))                   continue;
            if (!outsideBaseVision(tile, kBaseVisionPixels))        continue;

            if (dist > maxDist)
            {
                maxDist = dist;
                bestForPath = tile;
            }
        }

        // If we found a valid intercept point for this path, add it to the patrol list
        if (bestForPath.isValid())
        {
            // Deduplication: If multiple paths merge (e.g., at a natural expansion choke),
            // their best points will be identical or very close. Don't add duplicates.
            bool tooClose = false;
            for (const BWAPI::TilePosition& existingPoint : patrolPoints)
            {
                // If points are within the watch radius of each other, they cover the same area
                BWAPI::Position p1(bestForPath);
                BWAPI::Position p2(existingPoint);
                if (p1.getDistance(p2) < kWatchRadius * 4)
                {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose)
            {
                patrolPoints.push_back(bestForPath);
            }
        }
    }

    // --- 8. Record nearest path tile per approach for debug overlay ------
    // We now check if the path tile falls within the watch radius of ANY of our patrol points.
    for (ApproachPath& ap : _approachPaths)
    {
        ap.watchHit = BWAPI::TilePositions::Invalid; // Reset it

        for (const BWAPI::TilePosition& pt : ap.pathTiles)
        {
            bool foundHit = false;

            // Check against all of our generated patrol points
            for (const BWAPI::TilePosition& patrolPt : patrolPoints)
            {
                const int dx = patrolPt.x - pt.x;
                const int dy = patrolPt.y - pt.y;

                if (dx * dx + dy * dy <= kWatchRadius * kWatchRadius)
                {
                    ap.watchHit = pt;
                    foundHit = true;
                    break;
                }
            }

            // If we found a hit for this path, stop checking tiles for this path
            if (foundHit) break;
        }
    }

    // --- 7. Return the list of waypoints ---------------------------------
    return patrolPoints;
}

// ---------------------------------------------------------------------------
// Gradient-walk from startTile toward myBase using the pre-built distance map.
// At each step we move to the walkable 8-neighbor with the smallest distance
// value — this matches StarCraft's diagonal pathing much more closely.
// ---------------------------------------------------------------------------
std::vector<BWAPI::TilePosition> DistractorManager::tracePath(
    const BWAPI::TilePosition& startTile,
    const GridDistances& distFromBase) const
{
    // 8-way movement: East, West, South, North, SouthEast, SouthWest, NorthEast, NorthWest
    static const int dx[] = { 1, -1,  0,  0,  1, -1,  1, -1 };
    static const int dy[] = { 0,  0,  1, -1,  1,  1, -1, -1 };

    std::vector<BWAPI::TilePosition> path;

    // If the start tile itself is unreachable, bail.
    if (distFromBase.at(startTile) <= 0)
        return path;

    BWAPI::TilePosition current = startTile;
    int stepCount = 0;
    const int maxSteps = BWAPI::Broodwar->mapWidth() * BWAPI::Broodwar->mapHeight();

    path.push_back(current);

    while (distFromBase.at(current) > 0 && stepCount < maxSteps)
    {
        BWAPI::TilePosition best = BWAPI::TilePositions::Invalid;
        int bestDist = distFromBase.at(current);

        // Check all 8 neighbors instead of just 4
        for (int i = 0; i < 8; ++i)
        {
            BWAPI::TilePosition nb(current.x + dx[i], current.y + dy[i]);
            if (!nb.isValid()) continue;

            int d = distFromBase.at(nb);
            if (d >= 0 && d < bestDist)
            {
                bestDist = d;
                best = nb;
            }
        }

        if (!best.isValid()) break;   // stuck — unreachable or at base
        current = best;
        ++stepCount;

        // Sample every kTraceStep tiles to keep the path vector manageable
        // while still being dense enough for an accurate debug polyline.
        if (stepCount % kTraceStep == 0)
            path.push_back(current);
    }

    // Always include the final tile so the line reaches the base.
    if (path.empty() || path.back() != current)
        path.push_back(current);

    return path;
}

// ----------------------------------------------------------------------- ----
bool DistractorManager::outsideBaseVision(
    BWAPI::TilePosition tile,
    int visionPixels) const
{
    const BWAPI::Position tilePos(tile);
    for (const Base* base : the.bases.getAll())
    {
        if (!base || !base->getOwner()) continue;
        if (tilePos.getDistance(base->getCenter()) < static_cast<double>(visionPixels))
            return false;
    }
    return true;
}

BWAPI::Position DistractorManager::calculateLurePosition(const std::vector<BWAPI::Unit>& enemies)
{
    BWAPI::Position myPos = _unit->getPosition();
    BWAPI::Position bestPos = BWAPI::Positions::Invalid;
    double bestScore = -999999.0;

    // Check 16 radial directions
    for (int i = 0; i < 16; ++i)
    {
        double angle = i * (3.14159 * 2.0 / 16.0);
        int dx = static_cast<int>(std::cos(angle) * kLureStepPixels);
        int dy = static_cast<int>(std::sin(angle) * kLureStepPixels);

        BWAPI::Position candidate = myPos + BWAPI::Position(dx, dy);
        BWAPI::TilePosition candidateTile(candidate);

        // Discard off-map or unwalkable tiles
        if (!candidate.isValid() || !the.map.isTerrainWalkable(candidateTile))
            continue;

        double score = 0.0;

        // 1. Reward getting further away from enemies
        for (BWAPI::Unit enemy : enemies)
        {
            score += candidate.getDistance(enemy->getPosition());
        }

        // 2. Evaluate bases differently based on ownership
        for (const Base* base : the.bases.getAll())
        {
            if (!base || !base->getOwner()) continue;

            double distToBase = candidate.getDistance(base->getCenter());

            if (base->getOwner() == the.self())
            {
                // CONTINUOUS REWARD: Actively prefer directions that increase distance from OUR bases.
                // The 2.0 multiplier ensures that pulling the enemy away from our base 
                // overrides taking a path that might just be slightly further from the enemy.
                score += distToBase * 2.0;
            }
            else
            {
                // ENEMY BASES: Keep the threshold penalty so we don't accidentally lure 
                // the enemy right back into their own static defenses.
                if (distToBase < kBaseVisionPixels * 1.5)
                {
                    score -= (kBaseVisionPixels * 1.5 - distToBase) * 5.0;
                }
            }
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestPos = candidate;
        }
    }

    return bestPos;
}

void UAlbertaBot::DistractorManager::setCommand(MacroCommandType cmd)
{
    UAB_ASSERT(
        cmd == MacroCommandType::Distractor,
        "bad distractor command");

    _command = cmd;
    BWAPI::Broodwar->printf("Distract command SET");
}

bool DistractorManager::shouldDistract()
{

    if (_command == MacroCommandType::None)
    {
        return false;
    }

    return true;
}