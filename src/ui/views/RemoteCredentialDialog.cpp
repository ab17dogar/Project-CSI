#include "ui/views/RemoteCredentialDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/services/remote/RemoteCredentialsStore.h"

namespace Raytracer {
namespace ui {

RemoteCredentialDialog::RemoteCredentialDialog(RemoteCredentialsStore &store,
                                               const QVector<RemoteWorkerDefinition> &workers,
                                               QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_workers(workers)
{
    setWindowTitle(tr("Remote Worker Credentials"));
    setModal(true);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("Tokens are saved in the system keychain when available."
                                 " They are only used when dispatching remote jobs."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_workerCombo = new QComboBox(this);
    for (const auto &worker : m_workers) {
        QString label = worker.displayName;
        if (!worker.credentialKey.isEmpty()) {
            label = tr("%1 (%2)").arg(worker.displayName, worker.credentialKey);
        }
        m_workerCombo->addItem(label, worker.id);
    }

    auto *form = new QFormLayout();
    form->addRow(tr("Worker"), m_workerCombo);

    m_keyLabel = new QLabel(this);
    form->addRow(tr("Credential Key"), m_keyLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    form->addRow(tr("Status"), m_statusLabel);

    m_tokenEdit = new QLineEdit(this);
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setPlaceholderText(tr("Paste new token"));
    form->addRow(tr("New Token"), m_tokenEdit);

    layout->addLayout(form);

    auto *buttonRow = new QHBoxLayout();
    m_saveButton = new QPushButton(tr("Save Token"), this);
    m_deleteButton = new QPushButton(tr("Delete Token"), this);
    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);

    buttonRow->addWidget(m_saveButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch();
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_workerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RemoteCredentialDialog::handleWorkerChanged);
    connect(m_saveButton, &QPushButton::clicked, this, &RemoteCredentialDialog::handleSave);
    connect(m_deleteButton, &QPushButton::clicked, this, &RemoteCredentialDialog::handleDelete);

    if (m_workerCombo && m_workerCombo->count() > 0) {
        m_workerCombo->setCurrentIndex(0);
    } else if (m_workerCombo) {
        m_workerCombo->setEnabled(false);
    }
    refreshWorkerState();
}

void RemoteCredentialDialog::handleWorkerChanged(int)
{
    if (m_tokenEdit) {
        m_tokenEdit->clear();
    }
    refreshWorkerState();
}

void RemoteCredentialDialog::handleSave()
{
    const RemoteWorkerDefinition worker = currentWorker();
    if (worker.id.isEmpty()) {
        return;
    }
    if (worker.credentialKey.isEmpty()) {
        QMessageBox::information(this, tr("No Credential Key"),
                                 tr("The selected worker does not require a token."));
        return;
    }

    const QString token = m_tokenEdit ? m_tokenEdit->text().trimmed() : QString();
    if (token.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Token"), tr("Enter the token you want to store."));
        return;
    }

    QString error;
    if (!m_store.storeToken(worker.credentialKey, token, &error)) {
        QMessageBox::warning(this,
                             tr("Save Token Failed"),
                             error.isEmpty() ? tr("Unable to store the token.") : error);
        return;
    }

    if (m_tokenEdit) {
        m_tokenEdit->clear();
    }
    QMessageBox::information(this,
                             tr("Token Stored"),
                             tr("Credentials for %1 have been updated.").arg(worker.displayName));
    refreshWorkerState();
}

void RemoteCredentialDialog::handleDelete()
{
    const RemoteWorkerDefinition worker = currentWorker();
    if (worker.id.isEmpty() || worker.credentialKey.isEmpty()) {
        return;
    }

    if (QMessageBox::question(this,
                              tr("Remove Token"),
                              tr("Remove the stored token for %1?").arg(worker.displayName)) != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_store.removeToken(worker.credentialKey, &error)) {
        QMessageBox::warning(this,
                             tr("Remove Token Failed"),
                             error.isEmpty() ? tr("Unable to remove the token.") : error);
        return;
    }

    QMessageBox::information(this,
                             tr("Token Removed"),
                             tr("Credentials for %1 have been cleared.").arg(worker.displayName));
    refreshWorkerState();
}

void RemoteCredentialDialog::refreshWorkerState()
{
    if (!m_workerCombo || m_workers.isEmpty()) {
        if (m_keyLabel) {
            m_keyLabel->setText(tr("No workers available"));
        }
        if (m_statusLabel) {
            m_statusLabel->setText(tr("Configure remote workers with credential keys to manage tokens."));
        }
        if (m_tokenEdit) {
            m_tokenEdit->setEnabled(false);
        }
        if (m_saveButton) {
            m_saveButton->setEnabled(false);
        }
        if (m_deleteButton) {
            m_deleteButton->setEnabled(false);
        }
        return;
    }

    const int index = m_workerCombo->currentIndex();
    if (index < 0 || index >= m_workers.size()) {
        if (m_keyLabel) {
            m_keyLabel->setText(tr("Select a worker"));
        }
        if (m_statusLabel) {
            m_statusLabel->setText(QString());
        }
        if (m_saveButton) {
            m_saveButton->setEnabled(false);
        }
        if (m_deleteButton) {
            m_deleteButton->setEnabled(false);
        }
        if (m_tokenEdit) {
            m_tokenEdit->setEnabled(false);
        }
        return;
    }

    const auto &worker = m_workers.at(index);
    if (m_keyLabel) {
        m_keyLabel->setText(worker.credentialKey.isEmpty()
                                ? tr("Not required")
                                : worker.credentialKey);
    }

    if (worker.credentialKey.isEmpty()) {
        if (m_statusLabel) {
            m_statusLabel->setText(tr("This worker does not declare a credential key."));
        }
        if (m_tokenEdit) {
            m_tokenEdit->setEnabled(false);
        }
        if (m_saveButton) {
            m_saveButton->setEnabled(false);
        }
        if (m_deleteButton) {
            m_deleteButton->setEnabled(false);
        }
        return;
    }

    if (m_tokenEdit) {
        m_tokenEdit->setEnabled(true);
    }
    if (m_saveButton) {
        m_saveButton->setEnabled(true);
    }
    if (m_deleteButton) {
        m_deleteButton->setEnabled(true);
    }

    QString error;
    QString token = m_store.fetchToken(worker.credentialKey, &error);
    const bool hasToken = error.isEmpty() && !token.isEmpty();
    token.fill(QChar('\0'));
    token.clear();

    if (m_statusLabel) {
        if (hasToken) {
            m_statusLabel->setText(tr("Token is stored securely."));
        } else if (!error.isEmpty()) {
            m_statusLabel->setText(error);
        } else {
            m_statusLabel->setText(tr("No token stored."));
        }
    }
}

RemoteWorkerDefinition RemoteCredentialDialog::currentWorker() const
{
    if (!m_workerCombo) {
        return {};
    }
    const int index = m_workerCombo->currentIndex();
    if (index < 0 || index >= m_workers.size()) {
        return {};
    }
    return m_workers.at(index);
}

} // namespace ui
} // namespace Raytracer
