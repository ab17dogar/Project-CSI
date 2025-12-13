#pragma once

#include <QDialog>

#include "ui/models/PresetRepository.h"

class QLineEdit;
class QPlainTextEdit;

namespace Raytracer {
namespace ui {

class PresetEditorDialog : public QDialog
{
    Q_OBJECT

public:
    PresetEditorDialog(int width, int height, int samples, QWidget *parent = nullptr);

    void setInitialName(const QString &name);
    PresetRepository::Preset preset() const;

private slots:
    void validateAndAccept();

private:
    int m_width;
    int m_height;
    int m_samples;
    QLineEdit *m_nameEdit {nullptr};
    QLineEdit *m_authorEdit {nullptr};
    QPlainTextEdit *m_descriptionEdit {nullptr};
};

} // namespace ui
} // namespace Raytracer
