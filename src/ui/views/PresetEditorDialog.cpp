#include "ui/views/PresetEditorDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace Raytracer {
namespace ui {

PresetEditorDialog::PresetEditorDialog(int width, int height, int samples, QWidget *parent)
    : QDialog(parent)
    , m_width(width)
    , m_height(height)
    , m_samples(samples)
{
    setWindowTitle(tr("Save Render Profile"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    auto *summary = new QLabel(tr("Resolution %1 x %2 at %3 spp")
        .arg(width)
        .arg(height)
        .arg(samples),
        this);
    layout->addWidget(summary);

    auto *form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_authorEdit = new QLineEdit(this);
    m_descriptionEdit = new QPlainTextEdit(this);
    m_descriptionEdit->setMinimumHeight(60);
    m_descriptionEdit->setPlaceholderText(tr("Optional description"));

    form->addRow(tr("Name"), m_nameEdit);
    form->addRow(tr("Author"), m_authorEdit);
    form->addRow(tr("Description"), m_descriptionEdit);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PresetEditorDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PresetEditorDialog::reject);
    layout->addWidget(buttons);
}

void PresetEditorDialog::setInitialName(const QString &name)
{
    m_nameEdit->setText(name);
    m_nameEdit->selectAll();
}

PresetRepository::Preset PresetEditorDialog::preset() const
{
    PresetRepository::Preset preset;
    preset.name = m_nameEdit->text().trimmed();
    preset.width = m_width;
    preset.height = m_height;
    preset.samples = m_samples;
    preset.author = m_authorEdit->text().trimmed();
    preset.description = m_descriptionEdit->toPlainText().trimmed();
    preset.builtIn = false;
    return preset;
}

void PresetEditorDialog::validateAndAccept()
{
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Name"), tr("Enter a preset name."));
        return;
    }
    accept();
}

} // namespace ui
} // namespace Raytracer
