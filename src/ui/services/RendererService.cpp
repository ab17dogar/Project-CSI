#include "ui/services/RendererService.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QMetaType>
#include <QStringList>
#include <QThread>
#include <QtConcurrent>
#include <QtGlobal>
#include <QtGui/qrgb.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "engine/config.h"
#include "engine/factories/factory_methods.h"
#include "engine/render_runner.h"
#include "engine/world.h"
#include "ui/rendering/ToneMappingPresets.h"
#include "util/logging.h"
#include "util/util.h"

namespace Raytracer {
namespace ui {

namespace {

struct StageSettings {
  int width;
  int height;
  int samples;
  QString label;
  bool finalStage;
};

using ProgressCallback = std::function<void(
    const QImage &, const render::TileProgressStats &, int stageIndex,
    int totalStages, const StageSettings &stage)>;
using StageChangedCallback =
    std::function<void(const QString &, int stageIndex, int totalStages,
                       const StageSettings &stage)>;
using RenderResult = RendererService::RenderResult;

QString locateAssetsRoot() {
  QStringList probes;
  probes << QDir::currentPath();
  const QString appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty()) {
    probes << appDir;
    QDir walker(appDir);
    for (int i = 0; i < 4; ++i) {
      walker.cdUp();
      probes << walker.absolutePath();
    }
  }

  for (const QString &root : std::as_const(probes)) {
    if (root.isEmpty()) {
      continue;
    }
    QDir dir(root);
    if (dir.exists(QStringLiteral("assets"))) {
      return dir.filePath(QStringLiteral("assets"));
    }
  }
  return {};
}

QString findSceneInRoots(const QStringList &names) {
  QStringList roots;
  roots << QDir::currentPath();

  const QString appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty()) {
    QDir dir(appDir);
    roots << dir.absolutePath();
    roots << dir.filePath("..");
    roots << dir.filePath("../..");
  }

  // Remove duplicates while preserving order
  QStringList uniqueRoots;
  for (const QString &root : roots) {
    const QString normalized = QDir(root).absolutePath();
    if (!uniqueRoots.contains(normalized)) {
      uniqueRoots << normalized;
    }
  }

  const QString assetsRoot = locateAssetsRoot();
  if (!assetsRoot.isEmpty()) {
    roots << assetsRoot;
    QDir assetsDir(assetsRoot);
    if (assetsDir.exists(QStringLiteral("scenes"))) {
      roots << assetsDir.filePath(QStringLiteral("scenes"));
    }
    if (assetsDir.exists(QStringLiteral("imported"))) {
      roots << assetsDir.filePath(QStringLiteral("imported"));
    }
  }

  for (const QString &root : uniqueRoots) {
    QDir base(root);
    for (const QString &name : names) {
      const QString candidate = base.filePath(name);
      QFileInfo info(candidate);
      if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
      }
    }
  }
  return {};
}

struct RenderJobConfig {
  QString scenePath;
  int width;
  int height;
  int samples;
  bool useBVH{false};
};

class AtomicFlagGuard {
public:
  AtomicFlagGuard(std::atomic<bool> &flag, bool desired)
      : ref(flag), previous(flag.load()) {
    ref = desired;
  }

  ~AtomicFlagGuard() { ref = previous; }

private:
  std::atomic<bool> &ref;
  bool previous;
};

QString resolveScenePath(const QString &input) {
  const QString candidate =
      input.isEmpty() ? QStringLiteral("objects.xml") : input;
  QFileInfo info(candidate);

  if (info.isAbsolute()) {
    return info.absoluteFilePath();
  }

  if (info.exists()) {
    return info.absoluteFilePath();
  }

  QStringList nameVariants;
  nameVariants << candidate;
  if (candidate == QStringLiteral("objects.xml")) {
    nameVariants << QStringLiteral("build/objects.xml");
  }

  const QString resolved = findSceneInRoots(nameVariants);
  if (!resolved.isEmpty()) {
    return resolved;
  }

  // Fall back to the absolute path derived from the candidate so the error
  // message still shows where we attempted to load from.
  return info.absoluteFilePath();
}

std::vector<StageSettings> buildStages(const RenderJobConfig &config,
                                       world &sceneWorld) {
  int baseWidth = config.width > 0 ? config.width : sceneWorld.GetImageWidth();
  int baseHeight =
      config.height > 0 ? config.height : sceneWorld.GetImageHeight();
  const double aspectRatio =
      sceneWorld.GetAspectRatio() > 0.0
          ? sceneWorld.GetAspectRatio()
          : (baseHeight > 0 ? static_cast<double>(baseWidth) /
                                  static_cast<double>(baseHeight)
                            : 16.0 / 9.0);
  if (config.width > 0 && config.height <= 0) {
    baseHeight = static_cast<int>(
        std::round(static_cast<double>(baseWidth) / aspectRatio));
  } else if (config.height > 0 && config.width <= 0) {
    baseWidth = static_cast<int>(
        std::round(static_cast<double>(baseHeight) * aspectRatio));
  }
  const int baseSamples =
      config.samples > 0 ? config.samples : sceneWorld.GetSamplesPerPixel();

  std::vector<StageSettings> stages;

  const bool enablePreview =
      baseWidth >= 512 || baseHeight >= 512 || baseSamples >= 16;
  if (enablePreview) {
    const double aspect = aspectRatio;
    int previewWidth = std::max(256, baseWidth / 2);
    int previewHeight = static_cast<int>(
        std::round(static_cast<double>(previewWidth) / aspect));
    previewHeight = std::max(144, previewHeight);
    const int previewSamples = std::max(1, baseSamples / 4);
    stages.push_back({previewWidth, previewHeight, previewSamples,
                      QObject::tr("Preview"), false});
  }

  stages.push_back(
      {baseWidth, baseHeight, baseSamples, QObject::tr("Final"), true});
  return stages;
}

QImage bitmapToImage(const std::vector<color> &bitmap, int width, int height,
                     int samplesPerPixel) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  if (bitmap.size() < static_cast<size_t>(width * height)) {
    return {};
  }

  QImage image(width, height, QImage::Format_RGBA8888);
  const double sampleScale = 1.0 / std::max(1, samplesPerPixel);

  for (int y = 0; y < height; ++y) {
    QRgb *scanline = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const color &pixel = bitmap[static_cast<size_t>(y * width + x)];
      double r = pixel.x();
      double g = pixel.y();
      double b = pixel.z();

      if (r != r)
        r = 0.0;
      if (g != g)
        g = 0.0;
      if (b != b)
        b = 0.0;

      r = std::sqrt(sampleScale * r);
      g = std::sqrt(sampleScale * g);
      b = std::sqrt(sampleScale * b);

      const auto rc = static_cast<unsigned char>(256 * clamp(r, 0.0, 0.999));
      const auto gc = static_cast<unsigned char>(256 * clamp(g, 0.0, 0.999));
      const auto bc = static_cast<unsigned char>(256 * clamp(b, 0.0, 0.999));
      scanline[x] = qRgba(rc, gc, bc, 255);
    }
  }

  return image;
}

// For already normalized and gamma-corrected data (after OIDN denoising)
QImage bitmapToImageNormalized(const std::vector<color> &bitmap, int width,
                               int height) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  if (bitmap.size() < static_cast<size_t>(width * height)) {
    return {};
  }

  QImage image(width, height, QImage::Format_RGBA8888);

  for (int y = 0; y < height; ++y) {
    QRgb *scanline = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const color &pixel = bitmap[static_cast<size_t>(y * width + x)];
      double r = pixel.x();
      double g = pixel.y();
      double b = pixel.z();

      if (r != r)
        r = 0.0;
      if (g != g)
        g = 0.0;
      if (b != b)
        b = 0.0;

      // Data is already in [0,1] range, just convert to 8-bit
      const auto rc = static_cast<unsigned char>(256 * clamp(r, 0.0, 0.999));
      const auto gc = static_cast<unsigned char>(256 * clamp(g, 0.0, 0.999));
      const auto bc = static_cast<unsigned char>(256 * clamp(b, 0.0, 0.999));
      scanline[x] = qRgba(rc, gc, bc, 255);
    }
  }

  return image;
}

RenderResult runRenderJob(const RenderJobConfig &config,
                          std::atomic<bool> *cancelFlag,
                          ProgressCallback progressCallback,
                          StageChangedCallback stageChanged) {
  AtomicFlagGuard quietGuard(g_quiet, true);
  AtomicFlagGuard suppressGuard(g_suppress_mesh_messages, true);

  const QString resolvedPath = resolveScenePath(config.scenePath);
  const std::string scenePathUtf8 = resolvedPath.toStdString();

  RenderResult result;

  auto worldPtr = LoadScene(scenePathUtf8);
  if (!worldPtr) {
    const QString message =
        QObject::tr("Unable to load scene: %1").arg(resolvedPath);
    qWarning() << "RendererService:" << message;
    result.error = message;
    return result;
  }

  std::vector<color> bitmap;
  const unsigned int hwThreads = std::thread::hardware_concurrency();
  const int idealQtCount = QThread::idealThreadCount();
  const unsigned int fallback =
      idealQtCount > 0 ? static_cast<unsigned int>(idealQtCount) : 1u;
  const unsigned int threads = hwThreads ? hwThreads : fallback;

  // Set acceleration method based on config
  worldPtr->pconfig->acceleration =
      config.useBVH ? AccelerationMethod::BVH : AccelerationMethod::LINEAR;

  const auto stages = buildStages(config, *worldPtr);
  const int totalStages = static_cast<int>(stages.size());

  for (int stageIndex = 0; stageIndex < totalStages; ++stageIndex) {
    const StageSettings &stage = stages[stageIndex];

    if (stageChanged) {
      stageChanged(stage.label, stageIndex, totalStages, stage);
    }

    worldPtr->pconfig->IMAGE_WIDTH = stage.width;
    worldPtr->pconfig->IMAGE_HEIGHT = stage.height;
    worldPtr->pconfig->ASPECT_RATIO =
        stage.height > 0 ? static_cast<double>(stage.width) /
                               static_cast<double>(stage.height)
                         : worldPtr->GetAspectRatio();
    worldPtr->pconfig->SAMPLES_PER_PIXEL = stage.samples;

    auto lastNotified = std::make_shared<std::atomic<size_t>>(0);

    render::TileCallback tileCallback;
    if (progressCallback) {
      tileCallback = [progressCallback, lastNotified, cancelFlag,
                      stageWidth = stage.width, stageHeight = stage.height,
                      stageSamples = stage.samples, stageIndex, totalStages,
                      stage](const std::vector<color> &bitmap, int /*width*/,
                             int /*height*/,
                             const render::TileProgressStats &stats) {
        if (cancelFlag && cancelFlag->load()) {
          return;
        }
        const size_t stride = std::max<size_t>(1, stats.totalTiles / 32);
        bool shouldEmit = stats.tilesDone == stats.totalTiles;
        if (!shouldEmit) {
          size_t prev = lastNotified->load();
          if (stats.tilesDone >= prev + stride) {
            if (lastNotified->compare_exchange_strong(prev, stats.tilesDone)) {
              shouldEmit = true;
            }
          }
        } else {
          lastNotified->store(stats.tilesDone);
        }

        if (!shouldEmit) {
          return;
        }

        const QImage frame =
            bitmapToImage(bitmap, stageWidth, stageHeight, stageSamples);
        if (frame.isNull()) {
          return;
        }
        progressCallback(frame, stats, stageIndex, totalStages, stage);
      };
    }

    render::RenderSceneToBitmap(*worldPtr, bitmap, threads, 64, false,
                                tileCallback, cancelFlag);

    if (cancelFlag && cancelFlag->load()) {
      result.cancelled = true;
      return result;
    }

    if (stage.finalStage) {
      result.frame =
          bitmapToImage(bitmap, stage.width, stage.height, stage.samples);
      if (result.frame.isNull()) {
        result.error = QObject::tr("Render finished but produced no image.");
      }
    }
  }

  return result;
}

} // namespace

RendererService::RendererService(QObject *parent) : QObject(parent) {
  qRegisterMetaType<RenderTelemetry>(
      "Raytracer::ui::RendererService::RenderTelemetry");
  connect(&m_watcher, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    const RenderResult result = m_watcher.result();
    const RenderRequest request = m_activeRequest;
    const bool cancelled = result.cancelled || m_cancelRequested.load();

    if (!result.error.isEmpty()) {
      emit renderFailed(result.error);
    } else if (!result.frame.isNull() && !cancelled) {
      // const QImage finalFrame = applyPostProcessing(result.frame, request);
      const QImage finalFrame = result.frame;
      qDebug() << "Emitting FINAL denoised frame:" << finalFrame.width() << "x"
               << finalFrame.height();
      emit frameReady(finalFrame);
      emit progressUpdated(1.0, 0.0);
    }

    m_rendering = false;
    m_cancelRequested = false;
    emit renderFinished();
  });
}

void RendererService::startRender(const RenderRequest &request) {
  if (m_rendering.exchange(true)) {
    return;
  }

  RenderJobConfig config{request.scenePath, request.width, request.height,
                         request.samplesPerPixel, request.useBVH};
  m_activeRequest = request;

  m_cancelRequested = false;
  emit renderStarted();

  auto renderStart = std::make_shared<QElapsedTimer>();
  renderStart->start();
  auto stageStart = std::make_shared<QElapsedTimer>();
  stageStart->start();

  auto progressCallback =
      [this, renderStart, stageStart,
       request](const QImage &frame, const render::TileProgressStats &stats,
                int stageIndex, int totalStages, const StageSettings &stage) {
        const double stageCompletion =
            stats.totalTiles > 0 ? static_cast<double>(stats.tilesDone) /
                                       static_cast<double>(stats.totalTiles)
                                 : 0.0;
        const double overallCompletion =
            std::min(1.0, (static_cast<double>(stageIndex) + stageCompletion) /
                              std::max(1, totalStages));

        RendererService::RenderTelemetry telemetry;
        telemetry.scenePath = request.scenePath;
        telemetry.stageLabel = stage.label;
        telemetry.stageIndex = stageIndex;
        telemetry.totalStages = totalStages;
        telemetry.stageProgress = stageCompletion;
        telemetry.overallProgress = overallCompletion;
        telemetry.stageElapsedSeconds = stageStart->elapsed() / 1000.0;
        telemetry.elapsedSeconds = renderStart->elapsed() / 1000.0;
        telemetry.estimatedRemainingSeconds = stats.estRemainingMs / 1000.0;
        telemetry.avgTileMs = stats.avgTileMs;
        telemetry.tilesPerSecond =
            stats.avgTileMs > 0.0 ? 1000.0 / stats.avgTileMs : 0.0;
        telemetry.stageWidth = stage.width;
        telemetry.stageHeight = stage.height;
        telemetry.stageSamples = stage.samples;
        telemetry.finalStage = stage.finalStage;
        telemetry.useBVH = request.useBVH;

        // QImage processedFrame = applyPostProcessing(frame, request);
        QImage processedFrame = frame;

        QMetaObject::invokeMethod(
            this,
            [this, frame = std::move(processedFrame), stats, telemetry,
             stageCompletion]() {
              emit frameReady(frame);
              emit progressUpdated(telemetry.overallProgress,
                                   stats.estRemainingMs / 1000.0);
              emit telemetryUpdated(telemetry);
              if (telemetry.finalStage && stageCompletion >= 1.0) {
                emit progressUpdated(1.0, 0.0);
              }
            },
            Qt::QueuedConnection);
      };

  auto stageChanged =
      [this, stageStart](const QString &label, int /*stageIndex*/,
                         int /*totalStages*/, const StageSettings & /*stage*/) {
        stageStart->restart();
        QMetaObject::invokeMethod(
            this, [this, label]() { emit renderStageChanged(label); },
            Qt::QueuedConnection);
      };

  auto future = QtConcurrent::run([config, cancelFlag = &m_cancelRequested,
                                   progressCallback, stageChanged]() {
    return runRenderJob(config, cancelFlag, progressCallback, stageChanged);
  });
  m_watcher.setFuture(future);
}

#if 0
QImage RendererService::applyPostProcessing(const QImage &frame, const RenderRequest &request) const
{
    if (frame.isNull()) {
        return frame;
    }

    const bool needsToneMap = !request.toneMappingId.isEmpty() &&
        request.toneMappingId.compare(QStringLiteral("neutral"), Qt::CaseInsensitive) != 0;

    if (!request.enableDenoiser && !needsToneMap) {
        return frame;
    }

    QImage result = frame;
    if (request.enableDenoiser) {
        result = applyAdaptiveDenoise(result);
    }
    if (needsToneMap) {
        result = applyToneMapping(result, request.toneMappingId);
    }
    return result;
}

QImage RendererService::applyAdaptiveDenoise(const QImage &image) const
{
    if (image.isNull()) {
        return image;
    }

    QImage source = image.format() == QImage::Format_RGBA8888
        ? image
        : image.convertToFormat(QImage::Format_RGBA8888);
    QImage filtered(source.size(), QImage::Format_RGBA8888);

    const int width = source.width();
    const int height = source.height();
    if (width < 2 || height < 2) {
        return source;
    }

    static const int offsets[9][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    };
    static const double spatialWeights[9] = {1.0, 0.75, 0.75, 0.75, 0.75, 0.5, 0.5, 0.5, 0.5};
    constexpr double sigmaColor = 28.0;
    constexpr double denom = 2.0 * sigmaColor * sigmaColor;

    for (int y = 0; y < height; ++y) {
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *dstLine = reinterpret_cast<QRgb *>(filtered.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb center = srcLine[x];
            const double centerR = qRed(center);
            const double centerG = qGreen(center);
            const double centerB = qBlue(center);

            double sumR = 0.0;
            double sumG = 0.0;
            double sumB = 0.0;
            double weightSum = 0.0;

            for (int k = 0; k < 9; ++k) {
                const int nx = std::clamp(x + offsets[k][0], 0, width - 1);
                const int ny = std::clamp(y + offsets[k][1], 0, height - 1);
                const QRgb sample = reinterpret_cast<const QRgb *>(source.constScanLine(ny))[nx];

                const double diffR = centerR - qRed(sample);
                const double diffG = centerG - qGreen(sample);
                const double diffB = centerB - qBlue(sample);
                const double colorDistance = diffR * diffR + diffG * diffG + diffB * diffB;
                const double weight = spatialWeights[k] * std::exp(-colorDistance / denom);

                weightSum += weight;
                sumR += qRed(sample) * weight;
                sumG += qGreen(sample) * weight;
                sumB += qBlue(sample) * weight;
            }

            if (weightSum <= 0.0) {
                dstLine[x] = center;
                continue;
            }

            const auto newR = static_cast<int>(std::clamp(sumR / weightSum, 0.0, 255.0));
            const auto newG = static_cast<int>(std::clamp(sumG / weightSum, 0.0, 255.0));
            const auto newB = static_cast<int>(std::clamp(sumB / weightSum, 0.0, 255.0));
            dstLine[x] = qRgba(newR, newG, newB, qAlpha(center));
        }
    }

    return filtered;
}

QImage RendererService::applyToneMapping(const QImage &image, const QString &toneMappingId) const
{
    if (image.isNull()) {
        return image;
    }

    const auto *preset = tone::presetById(toneMappingId);
    if (!preset || preset->id.compare(QStringLiteral("neutral"), Qt::CaseInsensitive) == 0) {
        return image;
    }

    QImage source = image.format() == QImage::Format_RGBA8888
        ? image
        : image.convertToFormat(QImage::Format_RGBA8888);
    QImage mapped(source.size(), QImage::Format_RGBA8888);

    const int width = source.width();
    const int height = source.height();
    for (int y = 0; y < height; ++y) {
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *dstLine = reinterpret_cast<QRgb *>(mapped.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = srcLine[x];
            const int r = preset->lutR[static_cast<size_t>(qRed(pixel))];
            const int g = preset->lutG[static_cast<size_t>(qGreen(pixel))];
            const int b = preset->lutB[static_cast<size_t>(qBlue(pixel))];
            dstLine[x] = qRgba(r, g, b, qAlpha(pixel));
        }
    }

    return mapped;
}
#endif

void RendererService::stopRender() {
  if (!m_rendering.load()) {
    return;
  }

  m_cancelRequested = true;
  if (m_watcher.isRunning()) {
    m_watcher.waitForFinished();
  }
  // finished handler resets state and emits renderFinished
}

} // namespace ui
} // namespace Raytracer
