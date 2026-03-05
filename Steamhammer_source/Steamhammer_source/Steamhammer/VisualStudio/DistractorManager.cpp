#include "DistractorManager.h"
#include "Bases.h"
#include <The.h>

using namespace UAlbertaBot;

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

    Base* defensiveBase = the.bases.myNatural();

    // Fallback to main base if natural not taken
    if (!defensiveBase || the.self() != defensiveBase->getOwner())
    {
        defensiveBase = the.bases.myMain();
    }

    if (defensiveBase)
    {
        _targetTile = defensiveBase->getFrontTile();
    }
}

void DistractorManager::update()
{
    if (!_unit || !_unit->exists() || !_targetTile.isValid())
        return;

    BWAPI::Position targetPos(_targetTile);

    // Simple test behavior: move the unit toward the target
    if (_unit->getDistance(targetPos) > 32)
    {
        _unit->move(targetPos);
    }
}
