#pragma once

#include <QDialog>
#include <QVector>

#include "ui/services/remote/RemoteWorkerConfig.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace Raytracer {
namespace ui {

class RemoteCredentialsStore;

class RemoteCredentialDialog : public QDialog
{
    Q_OBJECT

public:
    RemoteCredentialDialog(RemoteCredentialsStore &store,
                           const QVector<RemoteWorkerDefinition> &workers,
                           QWidget *parent = nullptr);

private slots:
    void handleWorkerChanged(int index);
    void handleSave();
    void handleDelete();

private:
    void refreshWorkerState();
    RemoteWorkerDefinition currentWorker() const;

    RemoteCredentialsStore &m_store;
    QVector<RemoteWorkerDefinition> m_workers;
    QComboBox *m_workerCombo {nullptr};
    QLabel *m_keyLabel {nullptr};
    QLabel *m_statusLabel {nullptr};
    QLineEdit *m_tokenEdit {nullptr};
    QPushButton *m_saveButton {nullptr};
    QPushButton *m_deleteButton {nullptr};
};

} // namespace ui
} // namespace Raytracer
