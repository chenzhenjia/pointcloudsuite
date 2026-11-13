#pragma once

#include <pcv/camera/frame_data.h>
#include <pcv/core/point_types.h>
#include <QJsonObject>
#include <QSize>
#include <QVector>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <chrono>

namespace pcv::camera {

struct CaptureQueueStatistics {
    quint64 received = 0, receivedBytes = 0;
    quint64 intensityReceived = 0;
    size_t pending = 0, peakPending = 0;
    qsizetype pendingBytes = 0, peakBytes = 0;
    qint64 receiveWorkNs = 0, sdkConversionNs = 0, queueWaitNs = 0;
    quint32 lastWidth = 0, lastHeight = 0;
};

// Independent of the latest-frame preview slot. No I/O or GUI calls in push().
class CaptureFrameQueue {
public:
    explicit CaptureFrameQueue(quint32 rows, size_t maximumFrames = 2,
                               qsizetype maximumBytes = 512 * 1024 * 1024);
    explicit CaptureFrameQueue(CaptureConfiguration configuration, size_t maximumFrames = 2,
                               qsizetype maximumBytes = 512 * 1024 * 1024);
    bool push(std::shared_ptr<const CameraFrame> frame);
    std::shared_ptr<const CameraFrame> pop();
    void finish(); // Stop accepting, drain complete frames already accepted.
    void fail(const QString &reason);
    QString error() const;
    quint64 received() const;
    size_t pending() const;
    CaptureQueueStatistics statistics() const;
private:
    const CaptureConfiguration m_configuration;
    const size_t m_maximumFrames;
    const qsizetype m_maximumBytes;
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    struct Entry {
        std::shared_ptr<const CameraFrame> frame;
        std::chrono::steady_clock::time_point enqueued;
    };
    std::deque<Entry> m_frames;
    qsizetype m_bytes = 0;
    quint64 m_received = 0;
    bool m_accepting = true;
    QString m_error;
    CaptureQueueStatistics m_statistics;
};

struct CaptureFrameRecord {
    QString directory;
    quint64 id = 0;
    quint32 width = 0, height = 0, sdkFrameNumber = 0;
    qint64 timeStamp = 0;
    qsizetype pointCount = 0;
    bool intensityAvailable = false;
    qint64 pointConversionNs = 0, writeWorkNs = 0, savedBytes = 0;
    bool depthAvailable = false;
};
struct CaptureFrameData {
    CaptureFrameRecord record;
    QVector<pointcloud::Point3D> points;
    QVector<qsizetype> sourceIndices;
    QByteArray intensityMono8;
    quint32 intensityWidth = 0;
    quint32 intensityHeight = 0;
    QString intensityPngPath;
    QString error;
    bool ok = false;
    std::shared_ptr<const CameraFrame> rawFrame;
};

// All paths are session-relative. Raw depth browsing needs no SDK; XYZ does.
QString createCaptureSession(const QString &parent, quint32 rows,
                             const QString &device, QString *error);
QString createCaptureSession(const QString &parent, const CaptureConfiguration &configuration,
                             const QString &device, QString *error);
QVector<CaptureFrameRecord> readCaptureSession(const QString &directory, QString *error);
enum class CaptureFrameReadMode { WithIntensity, GeometryOnly, PreviewOnly, GeometryPreview };
// GeometryOnly validates all geometry/provenance but never reads intensity payloads.
CaptureFrameData readCapturedFrame(const QString &directory,
                                  const std::function<bool()> &cancelled = {},
                                  CaptureFrameReadMode mode = CaptureFrameReadMode::WithIntensity);
bool writeCaptureParameters(const QString &directory, const QJsonObject &parameters, QString *error);
QJsonObject readCaptureParameters(const QString &directory);

// Owns no source or GUI object. Raw-depth v3 and legacy ABC32f are explicit modes.
class CaptureRecorder {
public:
    CaptureRecorder(QString directory, quint32 rows);
    CaptureRecorder(QString directory, CaptureConfiguration configuration);
    ~CaptureRecorder();
    void finish();
    std::shared_ptr<CaptureFrameQueue> queue() const { return m_queue; }
    QVector<CaptureFrameRecord> savedFrames() const;
    QVector<pointcloud::Point3D> latestPreview() const;
    std::shared_ptr<const CameraFrame> latestDepthFrame() const;
    QByteArray latestIntensity() const;
    QSize latestIntensitySize() const;
    quint32 latestIntensityFrameNumber() const;
    struct Statistics {
        qint64 pointConversionNs = 0, writeWorkNs = 0, savedBytes = 0, elapsedNs = 0;
        qsizetype saved = 0;
        qsizetype intensitySaved = 0;
    };
    Statistics statistics() const;
private:
    void run();
    QString m_directory;
    CaptureConfiguration m_configuration;
    std::shared_ptr<CaptureFrameQueue> m_queue;
    mutable std::mutex m_mutex;
    QVector<CaptureFrameRecord> m_saved;
    QVector<pointcloud::Point3D> m_preview;
    std::shared_ptr<const CameraFrame> m_depthPreview;
    QByteArray m_latestIntensity;
    QSize m_latestIntensitySize;
    quint32 m_latestIntensityFrameNumber = 0;
    std::thread m_thread;
    const std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};
} // namespace pcv::camera
