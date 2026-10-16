#pragma once

#include <pcv/camera/frame_data.h>
#include <pcv/core/point_types.h>
#include <QJsonObject>
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
    qint64 pointConversionNs = 0, writeWorkNs = 0, savedBytes = 0;
};
struct CaptureFrameData {
    CaptureFrameRecord record;
    QVector<pointcloud::Point3D> points;
    QVector<qsizetype> sourceIndices;
    QString error;
    bool ok = false;
};

// All paths are session-relative on disk. Loading does not require an SDK.
QString createCaptureSession(const QString &parent, quint32 rows,
                             const QString &device, QString *error);
QString createCaptureSession(const QString &parent, const CaptureConfiguration &configuration,
                             const QString &device, QString *error);
QVector<CaptureFrameRecord> readCaptureSession(const QString &directory, QString *error);
CaptureFrameData readCapturedFrame(const QString &directory,
                                  const std::function<bool()> &cancelled = {});
bool writeCaptureParameters(const QString &directory, const QJsonObject &parameters, QString *error);
QJsonObject readCaptureParameters(const QString &directory);

// Owns no source or GUI object. Only normalized ABC32f frames enter the recorder.
class CaptureRecorder {
public:
    CaptureRecorder(QString directory, quint32 rows);
    CaptureRecorder(QString directory, CaptureConfiguration configuration);
    ~CaptureRecorder();
    void finish();
    std::shared_ptr<CaptureFrameQueue> queue() const { return m_queue; }
    QVector<CaptureFrameRecord> savedFrames() const;
    QVector<pointcloud::Point3D> latestPreview() const;
    struct Statistics {
        qint64 pointConversionNs = 0, writeWorkNs = 0, savedBytes = 0, elapsedNs = 0;
        qsizetype saved = 0;
    };
    Statistics statistics() const;
private:
    void run();
    QString m_directory;
    std::shared_ptr<CaptureFrameQueue> m_queue;
    mutable std::mutex m_mutex;
    QVector<CaptureFrameRecord> m_saved;
    QVector<pointcloud::Point3D> m_preview;
    std::thread m_thread;
    const std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};
} // namespace pcv::camera
