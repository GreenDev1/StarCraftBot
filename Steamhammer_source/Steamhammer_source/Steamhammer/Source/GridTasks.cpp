#include "GridTasks.h"

#include "Bases.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

void GroundHitsFixedTask::initialize()
{
    _name = "ground hits fixed";

    // There will be no fixed defense early on.
    _nextUpdateFrame = 1200 + 0;
}

void GroundHitsFixedTask::update()
{
    the.groundHitsFixed.update();
    _nextUpdateFrame += 19;
}

void GroundHitsFixedTask::draw() const
{
    //the.groundHitsFixed.draw();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

void AirHitsFixedTask::initialize()
{
    _name = "air hits fixed";

    // There will be no fixed defense early on.
    _nextUpdateFrame = 1200 + 1;
}

void AirHitsFixedTask::update()
{
    the.airHitsFixed.update();
    _nextUpdateFrame += 19;
}

void AirHitsFixedTask::draw() const
{
    //the.airHitsFixed.draw();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

void GroundHitsMobileTask::initialize()
{
    _name = "ground hits mobile";
    _nextUpdateFrame = 20 * 24 + 1;
}

void GroundHitsMobileTask::update()
{
    the.groundHitsMobile.update();
    _nextUpdateFrame += 3;
}

void GroundHitsMobileTask::draw() const
{
    //the.groundHitsMobile.draw();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

void AirHitsMobileTask::initialize()
{
    _name = "air hits mobile";
    _nextUpdateFrame = 20 * 24 + 6;
}

void AirHitsMobileTask::update()
{
    the.airHitsMobile.update();
    _nextUpdateFrame += 3;
}

void AirHitsMobileTask::draw() const
{
    //the.airHitsMobile.draw();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

GroundSafePathTask::GroundSafePathTask()
	: _influence(nullptr)
	, _baseIndex(0)
{
	_name = "ground safe path";
	_nextUpdateFrame = UpdateStartFrame;
}

void GroundSafePathTask::update()
{
	Base * base = the.bases.getAll().at(_baseIndex);

	// Update the paths.
	updateBase(base);

	// The next update frame.
	_nextUpdateFrame = (the.now() & ~1) + 2;		// update on even frames

	size_t nBases = the.bases.getAll().size();
	_baseIndex = (_baseIndex + 1) % nBases;
}

// Update the paths for a given base without worrying about other details.
// This is called from outside to do updates without going through the task system.
void GroundSafePathTask::updateBase(Base * base)
{
	if (!_influence)
	{
		_influence = new GroundInfluence();
	}
	_influence->update();
	base->getGroundSafePaths().update(_influence);
}

void GroundSafePathTask::draw() const
{
    if (the.now() > UpdateStartFrame)
    {
        //the.groundSafePath.draw();
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

AirSafePathTask::AirSafePathTask()
	: _influence(nullptr)
	, _baseIndex(0)
{
	_name = "air safe path";
	_nextUpdateFrame = UpdateStartFrame;
}

// The first updateBase() for some bases is called before the task is started!
// It's because the task is started on demand, and the demand may come at any time.
// There needs to be an initial update before the task gets around to it.
void AirSafePathTask::update()
{
	Base * base = the.bases.getAll().at(_baseIndex);

	// Update the paths.
	updateBase(base);

	// The next update frame.
	_nextUpdateFrame = (the.now() | 1) + 2;		// update on odd frames

	// Next base to update.
	size_t nBases = the.bases.getAll().size();
	_baseIndex = (_baseIndex + 1) % nBases;
}

// Update the paths for a given base without worrying about other details.
// This is called from outside to do updates without going through the task system.
// It's also called straight from update().
void AirSafePathTask::updateBase(Base * base)
{
	if (!_influence)
	{
		_influence = new AirInfluence();
	}
	_influence->update();
	base->getAirSafePaths().update(_influence);
}

void AirSafePathTask::draw() const
{
    if (the.now() > UpdateStartFrame)
    {
        //the.airSafePath.draw();
    }
}
