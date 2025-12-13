#pragma once

#include <QObject>
#include <memory>
#include <vector>
#include <functional>
#include "Command.h"

namespace Raytracer {
namespace scene {

/**
 * @brief Manages undo/redo history for commands.
 * 
 * Maintains a stack of executed commands and supports:
 * - Undo/redo operations
 * - Command merging (for combining rapid changes)
 * - History limits
 * - Clean state tracking
 */
class CommandHistory : public QObject
{
    Q_OBJECT

public:
    explicit CommandHistory(QObject *parent = nullptr);
    ~CommandHistory() override;
    
    /**
     * @brief Execute a command and add to history
     * @param command The command to execute (takes ownership)
     * @return true if command executed successfully
     */
    bool execute(std::unique_ptr<ICommand> command);
    
    /**
     * @brief Undo the most recent command
     * @return true if undo was successful
     */
    bool undo();
    
    /**
     * @brief Redo the most recently undone command
     * @return true if redo was successful
     */
    bool redo();
    
    /**
     * @brief Check if undo is available
     */
    bool canUndo() const;
    
    /**
     * @brief Check if redo is available
     */
    bool canRedo() const;
    
    /**
     * @brief Get description of command to be undone
     */
    QString undoDescription() const;
    
    /**
     * @brief Get description of command to be redone
     */
    QString redoDescription() const;
    
    /**
     * @brief Clear all history
     */
    void clear();
    
    /**
     * @brief Mark current state as clean (e.g., after save)
     */
    void setClean();
    
    /**
     * @brief Check if current state is clean
     */
    bool isClean() const;
    
    /**
     * @brief Get current undo stack size
     */
    int undoCount() const { return static_cast<int>(m_undoStack.size()); }
    
    /**
     * @brief Get current redo stack size
     */
    int redoCount() const { return static_cast<int>(m_redoStack.size()); }
    
    /**
     * @brief Set maximum history size (0 = unlimited)
     */
    void setMaxHistory(int max) { m_maxHistory = max; }
    int maxHistory() const { return m_maxHistory; }
    
    /**
     * @brief Enable/disable command merging
     */
    void setMergingEnabled(bool enabled) { m_mergingEnabled = enabled; }
    bool isMergingEnabled() const { return m_mergingEnabled; }
    
    /**
     * @brief Begin a macro (group multiple commands as one undo step)
     */
    void beginMacro(const QString &description);
    
    /**
     * @brief End current macro
     */
    void endMacro();
    
    /**
     * @brief Check if currently recording a macro
     */
    bool isInMacro() const { return m_macroDepth > 0; }

signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void undoDescriptionChanged(const QString &description);
    void redoDescriptionChanged(const QString &description);
    void cleanChanged(bool clean);
    void historyChanged();

private:
    void trimHistory();
    void emitStateChanged();
    
    std::vector<std::unique_ptr<ICommand>> m_undoStack;
    std::vector<std::unique_ptr<ICommand>> m_redoStack;
    
    int m_cleanIndex {0};       // Index where document was last saved
    int m_maxHistory {100};     // Max commands to keep
    bool m_mergingEnabled {true};
    
    // Macro support
    int m_macroDepth {0};
    QString m_macroDescription;
    std::vector<std::unique_ptr<ICommand>> m_macroCommands;
};

/**
 * @brief Helper class for macro command grouping
 */
class MacroCommand : public ICommand
{
public:
    explicit MacroCommand(const QString &description);
    
    void addCommand(std::unique_ptr<ICommand> command);
    
    bool execute() override;
    bool undo() override;
    bool redo() override;
    QString description() const override { return m_description; }
    
private:
    QString m_description;
    std::vector<std::unique_ptr<ICommand>> m_commands;
};

} // namespace scene
} // namespace Raytracer
