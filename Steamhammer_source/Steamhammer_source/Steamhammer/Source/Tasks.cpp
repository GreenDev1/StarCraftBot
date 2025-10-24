#include "Tasks.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Usage: post(new WhateverTask(...)).
// Start a new task.
void Tasks::post(Task * task)
{
    if (task->enabled() &&
		!get(task->getName()))		// not already started
    {
        task->initialize();
        tasks[task->getName()] = task;
    }
    else
    {
        delete task;
    }
}

// Delete the given task by name.
void Tasks::erase(const std::string & name)
{
	Task * task = get(name);
	if (task)
	{
		delete task;
	}
    tasks.erase(name);
}

void Tasks::update()
{
    for (std::pair<const std::string, Task *> & item : tasks)
    {
        Task * task = item.second;
        if (task->nextUpdate() <= the.now())
        {
            task->update();
            if (task->completed())
            {
                erase(item.first);
            }
        }
    }
}

Task * Tasks::get(const std::string & name) const
{
    for (const std::pair<const std::string, Task *> & item : tasks)
    {
        Task * task = item.second;
        if (task->getName() == name)
        {
            return task;
        }
    }

    return nullptr;
}

// Draw debugging info.
// Draw a table of what tasks exist.
// Then each task decides for itself whether to draw its own info, possibly controlled by different flags.
void Tasks::draw(int x, int y) const
{
    if (Config::Debug::DrawTasks)
    {
        for (const std::pair<const std::string, Task *> & item : tasks)
        {
            const std::string & name = item.first;
            const Task * task = item.second;
            BWAPI::Broodwar->drawTextScreen(x     , y, "%c%d", yellow, task->nextUpdate());
            BWAPI::Broodwar->drawTextScreen(x + 40, y, "%c%s", white , name.c_str());
            y += 10;
        }
    }

    for (const std::pair<const std::string, Task *> & item : tasks)
    {
        item.second->draw();
    }
}