#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>

#include <optional>
#include <functional>

namespace Raytracer {
namespace ui {

class RenderQueueModel : public QObject
{
    Q_OBJECT

public:
    enum class DispatchTarget {
        Local,
        Remote
    };

    struct QueuedRender {
        QUuid id;
        QString scenePath;
        int width {0};
        int height {0};
        int samples {0};
        QString presetLabel;
        QString outputPath;
        bool denoiserEnabled {false};
        QString toneMapping {QStringLiteral("neutral")};
        bool useBVH {false};
        DispatchTarget target {DispatchTarget::Local};
        QString remoteWorkerId;
        QString remoteWorkerLabel;
        QString remoteStatus;
        QString remoteJobId;
        QString remoteArtifact;
    };

    explicit RenderQueueModel(QObject *parent = nullptr);

    const QList<QueuedRender> &jobs() const noexcept { return m_jobs; }
    bool isEmpty() const noexcept { return m_jobs.isEmpty(); }
    int size() const noexcept { return m_jobs.size(); }

    QUuid enqueue(const QueuedRender &job);
    void prepend(const QueuedRender &job);
    std::optional<QueuedRender> takeNext();
    bool remove(const QUuid &id);
    void clear();
    bool update(const QUuid &id, const std::function<void(QueuedRender &)> &updater);

signals:
    void queueChanged();

private:
    QList<QueuedRender> m_jobs;
};

} // namespace ui
} // namespace Raytracer

Q_DECLARE_METATYPE(Raytracer::ui::RenderQueueModel::QueuedRender)
Q_DECLARE_METATYPE(Raytracer::ui::RenderQueueModel::DispatchTarget)
