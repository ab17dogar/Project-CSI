#pragma once

#include <QString>
#include <memory>

namespace Raytracer {
namespace scene {

class SceneDocument;

/**
 * @brief Abstract base class for undoable commands.
 * 
 * Commands encapsulate scene modifications and support undo/redo.
 * Each command stores enough state to reverse its operation.
 */
class ICommand
{
public:
    virtual ~ICommand() = default;
    
    /**
     * @brief Execute the command (first time or redo)
     * @return true if successful
     */
    virtual bool execute() = 0;
    
    /**
     * @brief Undo the command
     * @return true if successful
     */
    virtual bool undo() = 0;
    
    /**
     * @brief Redo the command (default calls execute)
     * @return true if successful
     */
    virtual bool redo() { return execute(); }
    
    /**
     * @brief Human-readable description for UI
     */
    virtual QString description() const = 0;
    
    /**
     * @brief Check if this command can merge with another
     * 
     * Used for combining rapid changes (e.g., dragging transforms)
     * into a single undo step.
     */
    virtual bool canMergeWith(const ICommand *other) const { 
        Q_UNUSED(other);
        return false; 
    }
    
    /**
     * @brief Merge another command into this one
     * @return true if merge successful
     */
    virtual bool mergeWith(const ICommand *other) { 
        Q_UNUSED(other);
        return false; 
    }
    
    /**
     * @brief Get the command type ID for merge checking
     */
    virtual int typeId() const { return 0; }
};

/**
 * @brief Base class for commands that operate on a SceneDocument
 */
class SceneCommand : public ICommand
{
public:
    explicit SceneCommand(SceneDocument *document)
        : m_document(document)
    {}
    
protected:
    SceneDocument *document() const { return m_document; }
    
private:
    SceneDocument *m_document;
};

} // namespace scene
} // namespace Raytracer
