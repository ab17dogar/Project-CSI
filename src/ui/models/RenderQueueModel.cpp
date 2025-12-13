#include "ui/models/RenderQueueModel.h"

#include <algorithm>

namespace Raytracer {
namespace ui {

RenderQueueModel::RenderQueueModel(QObject *parent)
    : QObject(parent)
{
}

QUuid RenderQueueModel::enqueue(const QueuedRender &job)
{
    QueuedRender stored = job;
    if (stored.id.isNull()) {
        stored.id = QUuid::createUuid();
    }
    m_jobs.append(stored);
    emit queueChanged();
    return stored.id;
}

void RenderQueueModel::prepend(const QueuedRender &job)
{
    QueuedRender stored = job;
    if (stored.id.isNull()) {
        stored.id = QUuid::createUuid();
    }
    m_jobs.prepend(stored);
    emit queueChanged();
}

std::optional<RenderQueueModel::QueuedRender> RenderQueueModel::takeNext()
{
    if (m_jobs.isEmpty()) {
        return std::nullopt;
    }
    QueuedRender job = m_jobs.takeFirst();
    emit queueChanged();
    return job;
}

bool RenderQueueModel::remove(const QUuid &id)
{
    auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [&id](const QueuedRender &job) {
        return job.id == id;
    });
    if (it == m_jobs.end()) {
        return false;
    }
    m_jobs.erase(it);
    emit queueChanged();
    return true;
}

void RenderQueueModel::clear()
{
    if (m_jobs.isEmpty()) {
        return;
    }
    m_jobs.clear();
    emit queueChanged();
}

bool RenderQueueModel::update(const QUuid &id, const std::function<void(QueuedRender &)> &updater)
{
    for (auto &job : m_jobs) {
        if (job.id == id) {
            updater(job);
            emit queueChanged();
            return true;
        }
    }
    return false;
}

} // namespace ui
} // namespace Raytracer
