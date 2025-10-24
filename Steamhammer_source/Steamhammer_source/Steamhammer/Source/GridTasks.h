#pragma once

#include "Task.h"

// Tasks for updating various grids.

namespace UAlbertaBot
{
// Forward declarations.
class Base;
class GridAttacks;

class GridTask : public Task
{
public:
    virtual void initialize() = 0;
    bool enabled() const { return true; };
};

class GroundHitsFixedTask : public GridTask
{
public:
    void initialize();
    void update();
    void draw() const;
};

class GroundHitsMobileTask : public GridTask
{
public:
    void initialize();
    void update();
    void draw() const;
};

class AirHitsFixedTask : public GridTask
{
public:
    void initialize();
    void update();
    void draw() const;
};

class AirHitsMobileTask : public GridTask
{
public:
    void initialize();
    void update();
    void draw() const;
};

// Update on even frames.
class GroundSafePathTask : public GridTask
{
private:
	const int UpdateStartFrame = 2500;		// start regular updates on this frame
	GridAttacks * _influence;
	int _baseIndex;
public:
	GroundSafePathTask();
	void initialize() {};
    void update();
	void updateBase(Base * base);
    void draw() const;
};

// Update on odd frames.
class AirSafePathTask : public GridTask
{
	const int UpdateStartFrame = 2501;		// start regular updates on this frame
	GridAttacks * _influence;
	int _baseIndex;
public:
	AirSafePathTask();
	void initialize() {};
    void update();
	void updateBase(Base * base);
	void draw() const;
};

}