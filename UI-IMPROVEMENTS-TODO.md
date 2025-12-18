# UI Improvements Implementation TODO

## Phase 1: Critical Fixes

### 1.1 Unsaved Changes Warning
- [ ] Add `m_isDirty` flag to `SceneDocument`
- [ ] Set dirty on any modification
- [ ] Clear dirty on save
- [ ] Show confirmation dialog in `MainWindow::closeEvent()`
- [ ] Show confirmation before New/Open when dirty

### 1.2 Undo/Redo System Foundation
- [ ] Create `UndoableCommand` base class
- [ ] Add `QUndoStack` to `SceneDocument`
- [ ] Create `ModifyPropertyCommand`
- [ ] Create `AddNodeCommand`
- [ ] Create `DeleteNodeCommand`
- [ ] Connect stack to Edit menu (Undo/Redo actions)
- [ ] Update SceneDocument methods to use commands

---

## Phase 2: Rendering Improvements

### 2.1 Progressive Rendering Display
- [ ] Modify `RendererService` to emit per-pass updates
- [ ] Update `RenderViewportWidget` to show progressive image
- [ ] Add sample count overlay

### 2.2 Render Region Selection
- [ ] Add rubber-band selection to `SceneViewport`
- [ ] Store crop region in render request
- [ ] Modify `RenderSceneToBitmap` to accept region
- [ ] Only render tiles within region

---

## Phase 3: Camera & Controls

### 3.1 Camera Orbit Smoothing
- [ ] Add animation timer for smooth orbit
- [ ] Implement damping/inertia
- [ ] Add focus on selection (F key)
- [ ] Improve pan sensitivity scaling

### 3.2 Keyboard Shortcuts
- [ ] F - Focus on selection
- [ ] Delete - Delete selected node
- [ ] Ctrl+D - Duplicate node
- [ ] Ctrl+Z/Y - Undo/Redo
- [ ] Ctrl+S - Save
- [ ] Ctrl+Shift+S - Save As

---

## Phase 4: Status & Feedback

### 4.1 Status Bar
- [ ] Add permanent status bar to MainWindow
- [ ] Show polygon count
- [ ] Show render progress %
- [ ] Show ETA during render
- [ ] Show current tool/mode

### 4.2 Recent Files Menu
- [ ] Store recent files in QSettings
- [ ] Add Recent Files submenu to File menu
- [ ] Load file on selection
- [ ] Clear recent files option

---

## Phase 5: Polish

### 5.1 Object Outliner Enhancements
- [ ] Enable drag-drop reordering
- [ ] Add multi-select support
- [ ] Add visibility toggle icons
- [ ] Add search/filter field

### 5.2 Material Editor Preview
- [ ] Add preview sphere widget
- [ ] Real-time preview updates
- [ ] Texture thumbnail display

---

## Completion Tracking

- [ ] Phase 1 Complete
- [ ] Phase 2 Complete
- [ ] Phase 3 Complete
- [ ] Phase 4 Complete
- [ ] Phase 5 Complete
