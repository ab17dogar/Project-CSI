#pragma once

#include <QObject>
#include <memory>

#include "ui/services/remote/RemoteWorkerConfig.h"
#include "ui/services/remote/RemoteDispatchCommon.h"

namespace Raytracer {
namespace ui {

class RemoteDispatchClient : public QObject
{
    Q_OBJECT

public:
    explicit RemoteDispatchClient(QObject *parent = nullptr) : QObject(parent) {}
    ~RemoteDispatchClient() override = default;

    virtual void start(const RemoteWorkerDefinition &worker,
                       const RemoteDispatchPayload &payload) = 0;
    virtual void cancel() = 0;

signals:
    void started(const QString &jobId);
    void progress(const QString &jobId, double value, const QString &label);
    void completed(const RemoteDispatchResult &result);
    void failed(const QString &jobId, const QString &message);
};

using RemoteDispatchClientPtr = std::unique_ptr<RemoteDispatchClient>;

} // namespace ui
} // namespace Raytracer
