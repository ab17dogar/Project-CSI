#include "ui/views/MainWindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[]) {
  // Set default OpenGL format before creating QApplication
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  format.setSamples(4);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);

  // Professional dark theme inspired by Unity/Unreal
  app.setStyleSheet(R"(
        /* Main application colors */
        QMainWindow, QDialog, QWidget {
            background-color: #1e1e1e;
            color: #e0e0e0;
        }
        
        /* Group boxes with accent borders */
        QGroupBox {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            margin-top: 12px;
            padding-top: 8px;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 5px;
            color: #0ea5e9;
        }
        
        /* Modern buttons */
        QPushButton {
            background-color: #3c3c3c;
            border: 1px solid #4a4a4a;
            border-radius: 4px;
            padding: 6px 16px;
            color: #e0e0e0;
            min-height: 20px;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
            border-color: #0ea5e9;
        }
        QPushButton:pressed {
            background-color: #0ea5e9;
            color: white;
        }
        QPushButton:disabled {
            background-color: #2d2d2d;
            color: #666;
        }
        
        /* Primary action buttons */
        QPushButton#startButton, QPushButton#renderButton {
            background-color: #0ea5e9;
            color: white;
            font-weight: bold;
        }
        QPushButton#startButton:hover, QPushButton#renderButton:hover {
            background-color: #38bdf8;
        }
        
        /* Input fields */
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: #2d2d2d;
            border: 1px solid #3c3c3c;
            border-radius: 3px;
            padding: 4px 8px;
            color: #e0e0e0;
            selection-background-color: #0ea5e9;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border-color: #0ea5e9;
        }
        
        /* Combo box dropdown */
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background-color: #2d2d2d;
            border: 1px solid #3c3c3c;
            selection-background-color: #0ea5e9;
        }
        
        /* Tab widget */
        QTabWidget::pane {
            border: 1px solid #3c3c3c;
            background-color: #1e1e1e;
        }
        QTabBar::tab {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-bottom: none;
            padding: 8px 16px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: #1e1e1e;
            border-bottom: 2px solid #0ea5e9;
        }
        QTabBar::tab:hover:!selected {
            background-color: #2d2d2d;
        }
        
        /* List widgets */
        QListWidget {
            background-color: #2d2d2d;
            border: 1px solid #3c3c3c;
            border-radius: 3px;
        }
        QListWidget::item {
            padding: 4px;
            border-radius: 2px;
        }
        QListWidget::item:selected {
            background-color: #0ea5e9;
        }
        QListWidget::item:hover:!selected {
            background-color: #3c3c3c;
        }
        
        /* Labels */
        QLabel {
            color: #b0b0b0;
        }
        
        /* Scroll bars */
        QScrollBar:vertical {
            background-color: #1e1e1e;
            width: 12px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background-color: #3c3c3c;
            border-radius: 6px;
            min-height: 30px;
            margin: 2px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #4a4a4a;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            height: 0;
        }
        
        /* Progress bar */
        QProgressBar {
            background-color: #2d2d2d;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            height: 16px;
            text-align: center;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #0ea5e9, stop:1 #38bdf8);
            border-radius: 3px;
        }
        
        /* Menu bar */
        QMenuBar {
            background-color: #252526;
            border-bottom: 1px solid #3c3c3c;
        }
        QMenuBar::item {
            padding: 6px 12px;
        }
        QMenuBar::item:selected {
            background-color: #3c3c3c;
        }
        QMenu {
            background-color: #252526;
            border: 1px solid #3c3c3c;
        }
        QMenu::item {
            padding: 6px 24px;
        }
        QMenu::item:selected {
            background-color: #0ea5e9;
        }
        
        /* Splitter handle */
        QSplitter::handle {
            background-color: #3c3c3c;
        }
        QSplitter::handle:horizontal {
            width: 2px;
        }
        QSplitter::handle:vertical {
            height: 2px;
        }
        
        /* Toolbar */
        QToolBar {
            background-color: #252526;
            border: none;
            spacing: 4px;
            padding: 4px;
        }
        QToolButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 6px;
        }
        QToolButton:hover {
            background-color: #3c3c3c;
            border-color: #4a4a4a;
        }
        QToolButton:pressed {
            background-color: #0ea5e9;
        }
        
        /* Status bar */
        QStatusBar {
            background-color: #252526;
            border-top: 1px solid #3c3c3c;
        }
    )");

  Raytracer::ui::MainWindow mainWindow;
  mainWindow.show();
  return app.exec();
}
