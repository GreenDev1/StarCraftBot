#pragma once

#include "BWAPI.h"
#include <vector>

namespace UAlbertaBot
{
    class DistractorManager
    {
        BWAPI::Unit _unit;                   // The unit used as distractor
        BWAPI::TilePosition _targetTile;     // Position to move to

    public:
        DistractorManager();

        void assignUnit(BWAPI::Unit unit);

        void update();                       // Call every frame
    };
}
