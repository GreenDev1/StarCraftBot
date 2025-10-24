#pragma once

#include "Task.h"

#include <map>

namespace UAlbertaBot
{
class GameRecord;

class Tasks
{
private:
    std::map<std::string, Task *> tasks;

public:

    void post(Task * Task);
    void erase(const std::string & name);

    void update();

    size_t size() const { return tasks.size(); };
    Task * get(const std::string & name) const;

    void draw(int x, int y) const;
};

}