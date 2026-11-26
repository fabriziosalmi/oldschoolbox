#pragma once
#include <vector>
#include "Scenes.h"
#include "../Sequencer.h"

// UI-thread Undo/Redo stack for sequencer edits (patterns, velocities,
// probabilities, micro-timing, lengths, humanize, and 303 bass attributes/notes).
class SequencerUndo
{
public:
    static constexpr size_t MAX_DEPTH = 32;

    void push(const Sequencer& seq)
    {
        Scenes::Snapshot snap;
        Scenes::capture(snap, seq);
        if (undoStack.size() >= MAX_DEPTH)
            undoStack.erase(undoStack.begin());
        undoStack.push_back(snap);
        redoStack.clear();
    }

    bool undo(Sequencer& seq)
    {
        if (undoStack.empty()) return false;
        Scenes::Snapshot current;
        Scenes::capture(current, seq);
        redoStack.push_back(current);

        Scenes::apply(undoStack.back(), seq);
        undoStack.pop_back();
        return true;
    }

    bool redo(Sequencer& seq)
    {
        if (redoStack.empty()) return false;
        Scenes::Snapshot current;
        Scenes::capture(current, seq);
        undoStack.push_back(current);

        Scenes::apply(redoStack.back(), seq);
        redoStack.pop_back();
        return true;
    }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    void clear()
    {
        undoStack.clear();
        redoStack.clear();
    }

private:
    std::vector<Scenes::Snapshot> undoStack;
    std::vector<Scenes::Snapshot> redoStack;
};
