\
#include "The.h"

#include "Bases.h"
#include "GridTasks.h"
#include "InformationManager.h"
#include "MapGrid.h"
#include "OpeningTiming.h"
#include "OpponentModel.h"
#include "ParseUtils.h"
#include "ProductionManager.h"
#include "Random.h"
#include "StaticDefense.h"

using namespace UAlbertaBot;

The::The()
    : bases(Bases::Instance())
    , grid(MapGrid::Instance())
    , info(InformationManager::Instance())
    , openingTiming(OpeningTiming::Instance())
    , production(ProductionManager::Instance())
    , random(Random::Instance())
    , staticDefense(*new StaticDefense)
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// NOTE The skill kit is initialized in OpponentModel and updated in GameCommander.

void The::initialize()
{
    _selfRace = BWAPI::Broodwar->self()->getRace();

    // The order of initialization is important because of dependencies.
    partitions.initialize();
    inset.initialize();				// depends on partitions
    vWalkRoom.initialize();			// depends on inset
    tileRoom.initialize();			// depends on vWalkRoom
    zone.initialize();				// depends on tileRoom
    map.initialize();

    bases.initialize();             // depends on map
    info.initialize();              // depends on bases
    placer.initialize();
    ops.initialize();

    // Read the opening timing file, to make opening decisions.
    // openingTiming.read();

    // Parse the bot's configuration file.
    // Change this file path (in config.cpp) to point to your config file.
    // Any relative path name will be relative to Starcraft installation folder.
    // The config depends on the map and must be read after the map is analyzed.
    // This also reads the opponent model data and decides on the opening.
    ParseUtils::ParseConfigFile(Config::ConfigFile::ConfigFileLocation);

    // Sets the initial production queue to the book opening chosen above.
    production.initialize();

    // Add tasks that will run throughout the game.
    tasks.post(new GroundHitsFixedTask());
    tasks.post(new GroundHitsMobileTask());
	tasks.post(new GroundSafePathTask());
	// The air safe path task is started only if and when we get air units.
	// See maybeStartAirSafePathTask().
}

// Hits from static defense, sieged tanks, and burrowed lurkers.
int The::staticHits(BWAPI::Unit unit, const BWAPI::TilePosition & tile) const
{
    return unit->isFlying()
        ? airHitsFixed.at(tile)
        : groundHitsFixed.at(tile);
}

// Hits from static defense, sieged tanks, and burrowed lurkers.
int The::staticHits(BWAPI::Unit unit) const
{
    return unit->isFlying()
        ? airHitsFixed.at(unit->getTilePosition())
        : groundHitsFixed.at(unit->getTilePosition());
}

// Hits from all enemies on ground units, static and mobile.
int The::groundHits(const BWAPI::TilePosition & tile) const
{
	return groundHitsFixed.at(tile) + groundHitsMobile.at(tile);
}

// Hits from all enemies on air units, static and mobile.
int The::airHits(const BWAPI::TilePosition & tile) const
{
	return airHitsFixed.at(tile) + airHitsMobile.at(tile);
}

// Air or ground hits.
int The::hits(bool air, const BWAPI::TilePosition & tile) const
{
	return air ? the.airHits(tile) > 0 : the.groundHits(tile);
}

// Air or ground hits.
int The::hits(bool air, const BWAPI::Position & pos) const
{
	return hits(air, BWAPI::TilePosition(pos));
}

// Hits from all enemies, static and mobile.
int The::hits(BWAPI::Unit unit, const BWAPI::TilePosition& tile) const
{
    return unit->isFlying()
        ? airHits(unit->getTilePosition())
        : groundHits(unit->getTilePosition());
}

// Hits from all enemies, static and mobile.
int The::hits(BWAPI::Unit unit) const
{
    return unit->isFlying()
        ? airHits(unit->getTilePosition())
        : groundHits(unit->getTilePosition());
}

// Start the tasks to compute various aspects of air safety for our units.
// Zerg has overlords and starts the tasks immediately.
void The::maybeStartAirTasks()
{
	if (!_airTasksStarted && the.info.weHaveAirUnits())
	{
		tasks.post(new AirHitsFixedTask());
		tasks.post(new AirHitsMobileTask());
		tasks.post(new AirSafePathTask());
		_airTasksStarted = true;
	}
}

void The::update()
{
    my.completed.takeSelf();
    my.all.takeSelfAll();
    your.seen.takeEnemy();
    your.ever.takeEnemyEver(your.seen);
    your.inferred.takeEnemyInferred(your.ever);

    ops.update();
    staticDefense.update();
    grid.update();

	maybeStartAirTasks();
}

OpponentModel & The::oppModel()
{
	return OpponentModel::Instance();
}
