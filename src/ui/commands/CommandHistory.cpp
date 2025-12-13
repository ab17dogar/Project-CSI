#include "CommandHistory.h"
#include "Command.h"

namespace Raytracer {
namespace scene {

// ===== CommandHistory =====

CommandHistory::CommandHistory(QObject *parent) : QObject(parent) {}

CommandHistory::~CommandHistory() = default;

bool CommandHistory::execute(std::unique_ptr<ICommand> command) {
  if (!command)
    return false;

  // Handle macro recording
  if (isInMacro()) {
    if (command->execute()) {
      m_macroCommands.push_back(std::move(command));
      return true;
    }
    return false;
  }

  // Execute the command FIRST so the change is applied
  if (!command->execute()) {
    return false;
  }

  // Try to merge with previous command (after execution)
  if (m_mergingEnabled && !m_undoStack.empty()) {
    ICommand *last = m_undoStack.back().get();
    if (last->canMergeWith(command.get())) {
      if (last->mergeWith(command.get())) {
        // Merged successfully, don't add new command to stack
        // (command was already executed above)
        emitStateChanged();
        return true;
      }
    }
  }

  // Add to undo stack
  m_undoStack.push_back(std::move(command));

  // Clear redo stack (new action invalidates redo history)
  m_redoStack.clear();

  // Trim if over limit
  trimHistory();

  emitStateChanged();
  return true;
}

bool CommandHistory::undo() {
  if (!canUndo())
    return false;

  auto command = std::move(m_undoStack.back());
  m_undoStack.pop_back();

  if (command->undo()) {
    m_redoStack.push_back(std::move(command));
    emitStateChanged();
    return true;
  }

  // Undo failed, put command back
  m_undoStack.push_back(std::move(command));
  return false;
}

bool CommandHistory::redo() {
  if (!canRedo())
    return false;

  auto command = std::move(m_redoStack.back());
  m_redoStack.pop_back();

  if (command->redo()) {
    m_undoStack.push_back(std::move(command));
    emitStateChanged();
    return true;
  }

  // Redo failed, put command back
  m_redoStack.push_back(std::move(command));
  return false;
}

bool CommandHistory::canUndo() const {
  return !m_undoStack.empty() && !isInMacro();
}

bool CommandHistory::canRedo() const {
  return !m_redoStack.empty() && !isInMacro();
}

QString CommandHistory::undoDescription() const {
  if (m_undoStack.empty())
    return QString();
  return m_undoStack.back()->description();
}

QString CommandHistory::redoDescription() const {
  if (m_redoStack.empty())
    return QString();
  return m_redoStack.back()->description();
}

void CommandHistory::clear() {
  m_undoStack.clear();
  m_redoStack.clear();
  m_cleanIndex = 0;
  emitStateChanged();
}

void CommandHistory::setClean() {
  m_cleanIndex = static_cast<int>(m_undoStack.size());
  emit cleanChanged(true);
}

bool CommandHistory::isClean() const {
  return static_cast<int>(m_undoStack.size()) == m_cleanIndex;
}

void CommandHistory::beginMacro(const QString &description) {
  if (m_macroDepth == 0) {
    m_macroDescription = description;
    m_macroCommands.clear();
  }
  m_macroDepth++;
}

void CommandHistory::endMacro() {
  if (m_macroDepth == 0)
    return;

  m_macroDepth--;

  if (m_macroDepth == 0 && !m_macroCommands.empty()) {
    auto macro = std::make_unique<MacroCommand>(m_macroDescription);
    for (auto &cmd : m_macroCommands) {
      macro->addCommand(std::move(cmd));
    }
    m_macroCommands.clear();

    // Add macro to undo stack (already executed)
    m_undoStack.push_back(std::move(macro));
    m_redoStack.clear();
    trimHistory();
    emitStateChanged();
  }
}

void CommandHistory::trimHistory() {
  if (m_maxHistory <= 0)
    return;

  while (static_cast<int>(m_undoStack.size()) > m_maxHistory) {
    m_undoStack.erase(m_undoStack.begin());
    if (m_cleanIndex > 0)
      m_cleanIndex--;
  }
}

void CommandHistory::emitStateChanged() {
  emit canUndoChanged(canUndo());
  emit canRedoChanged(canRedo());
  emit undoDescriptionChanged(undoDescription());
  emit redoDescriptionChanged(redoDescription());
  emit cleanChanged(isClean());
  emit historyChanged();
}

// ===== MacroCommand =====

MacroCommand::MacroCommand(const QString &description)
    : m_description(description) {}

void MacroCommand::addCommand(std::unique_ptr<ICommand> command) {
  m_commands.push_back(std::move(command));
}

bool MacroCommand::execute() {
  // Commands are already executed when added during macro recording
  return true;
}

bool MacroCommand::undo() {
  // Undo in reverse order
  for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
    if (!(*it)->undo()) {
      // Try to re-execute commands that were undone
      for (auto rit = it; rit != m_commands.rbegin();) {
        --rit;
        (*rit)->redo();
      }
      return false;
    }
  }
  return true;
}

bool MacroCommand::redo() {
  for (auto &cmd : m_commands) {
    if (!cmd->redo()) {
      // Undo commands that were redone
      for (auto it = m_commands.begin(); it->get() != cmd.get(); ++it) {
        (*it)->undo();
      }
      return false;
    }
  }
  return true;
}

} // namespace scene
} // namespace Raytracer
