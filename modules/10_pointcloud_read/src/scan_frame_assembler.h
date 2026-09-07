#pragma once

#include <pcv/camera/frame_data.h>

#include <QByteArray>

#include <cstdint>
#include <limits>
#include <memory>

namespace pcv::camera::detail {

enum class ScanAssemblyOutcome {
    Pending,
    Complete,
    Rejected
};

enum class ExpectedScanHeightSource {
    Maximum,
    Current
};

struct ExpectedScanHeightSelection {
    std::uint32_t rows = 0;
    ExpectedScanHeightSource source = ExpectedScanHeightSource::Current;
};

// 3DMVS exposes Height.nCurValue as the current transfer batch on some
// HFR devices, while nMax is the sensor buffer/full-scan capacity. Prefer
// the documented maximum and only fall back when it is unavailable.
inline ExpectedScanHeightSelection selectExpectedScanHeight(qint64 current,
                                                             qint64 maximum)
{
    const qint64 selected = maximum > 0 ? maximum : current;
    const auto rows = selected > 0
        && selected <= qint64(std::numeric_limits<std::uint32_t>::max())
        ? std::uint32_t(selected)
        : 0u;
    return {rows, maximum > 0 ? ExpectedScanHeightSource::Maximum
                              : ExpectedScanHeightSource::Current};
}

struct ScanAssemblyResult {
    ScanAssemblyOutcome outcome = ScanAssemblyOutcome::Pending;
    std::shared_ptr<CameraFrame> frame;
    QString error;
};

// Reassembles the profile batches emitted by DP-series HFR devices into one
// complete ABC32f scan. DP-series devices use nFrameNum as a monotonically
// increasing profile-row sequence, not a per-scan row index. The first batch
// anchors a scan at any sequence value; a discontinuity invalidates the
// current scan rather than publishing geometry with a missing band.
class ScanFrameAssembler {
public:
    static constexpr qsizetype kDefaultMaximumPayloadBytes = 256 * 1024 * 1024;

    explicit ScanFrameAssembler(std::uint32_t expectedRows = 0,
                                qsizetype maximumPayloadBytes = kDefaultMaximumPayloadBytes)
        : m_expectedRows(expectedRows)
        , m_maximumPayloadBytes(maximumPayloadBytes)
    {
    }

    void configure(std::uint32_t expectedRows)
    {
        m_expectedRows = expectedRows;
        reset();
    }

    void reset()
    {
        m_width = 0;
        m_accumulatedRows = 0;
        m_nextFrameNumber = 0;
        m_fragmentCount = 0;
        m_active = false;
        m_data.clear();
    }

    std::uint32_t expectedRows() const { return m_expectedRows; }
    std::uint32_t accumulatedRows() const { return m_accumulatedRows; }
    std::uint32_t fragmentCount() const { return m_fragmentCount; }
    std::uint32_t discardedScanCount() const { return m_discardedScanCount; }

    ScanAssemblyResult append(const std::shared_ptr<CameraFrame> &chunk)
    {
        if (!chunk || chunk->frameType != FrameType::PointCloudABC32f
            || !chunk->valid || chunk->width == 0 || chunk->height == 0
            || chunk->data.size() % 12 != 0
            || chunk->data.size() / 12 != qsizetype(chunk->width) * qsizetype(chunk->height)) {
            discardActiveScan();
            return reject(QStringLiteral("normalized scan chunk is not a valid ABC32f raster"));
        }

        // A single callback may already carry the complete scan (DP-series
        // depth frames are emitted whole). When no fragment is being
        // accumulated, treat any self-contained chunk as complete and adopt
        // its height when it exceeds the configured expectation (the Height
        // parameter is not a hard upper bound on DP HFR devices).
        if (m_expectedRows == 0 || chunk->height == m_expectedRows) {
            if (m_active) discardActiveScan();
            if (chunk->height > m_expectedRows) m_expectedRows = chunk->height;
            return completeDirect(chunk);
        }
        if (chunk->height > m_expectedRows) {
            if (m_active) {
                discardActiveScan();
                return reject(QStringLiteral("scan chunk height %1 exceeds configured Height %2")
                                  .arg(chunk->height).arg(m_expectedRows));
            }
            // No fragment in flight: adopt the observed height and publish.
            m_expectedRows = chunk->height;
            return completeDirect(chunk);
        }

        if (!m_active) {
            if (!begin(chunk)) {
                return reject(QStringLiteral("complete scan payload exceeds assembly memory limit"));
            }
        } else if (chunk->width != m_width) {
            discardActiveScan();
            return reject(QStringLiteral("scan chunk width changed within one acquisition"));
        } else if (chunk->frameNumber != m_nextFrameNumber) {
            discardActiveScan();
            return reject(QStringLiteral("scan chunk profile rows are discontinuous"));
        }

        if (m_accumulatedRows + chunk->height > m_expectedRows) {
            discardActiveScan();
            return reject(QStringLiteral("scan accumulated rows %1 + chunk height %2 exceeds configured Height %3")
                              .arg(m_accumulatedRows).arg(chunk->height).arg(m_expectedRows));
        }
        if (chunk->data.size() > m_maximumPayloadBytes - m_data.size()) {
            discardActiveScan();
            return reject(QStringLiteral("scan payload exceeds assembly memory limit"));
        }

        m_data.append(chunk->data);
        m_accumulatedRows += chunk->height;
        m_nextFrameNumber = chunk->frameNumber + chunk->height;
        ++m_fragmentCount;
        m_lastChunk = chunk;
        if (m_accumulatedRows != m_expectedRows) return {};

        auto completed = std::make_shared<CameraFrame>(*m_lastChunk);
        completed->width = m_width;
        completed->height = m_expectedRows;
        completed->data = std::move(m_data);
        completed->intensityData.clear();
        completed->frameNumber = ++m_completedScanCount;
        completed->expectedScanRows = m_expectedRows;
        completed->accumulatedScanRows = m_expectedRows;
        completed->scanFragmentCount = m_fragmentCount;
        completed->discardedScanCount = m_discardedScanCount;
        completed->completeScan = true;
        reset();
        m_lastChunk.reset();
        return {ScanAssemblyOutcome::Complete, std::move(completed), {}};
    }

private:
    bool begin(const std::shared_ptr<CameraFrame> &chunk)
    {
        const std::uint64_t expectedBytes = std::uint64_t(chunk->width)
            * std::uint64_t(m_expectedRows) * 12u;
        if (expectedBytes > std::uint64_t(m_maximumPayloadBytes)
            || expectedBytes > std::uint64_t(std::numeric_limits<qsizetype>::max())) {
            reset();
            return false;
        }
        m_width = chunk->width;
        m_accumulatedRows = 0;
        m_nextFrameNumber = chunk->frameNumber;
        m_fragmentCount = 0;
        m_active = true;
        m_data.clear();
        m_data.reserve(qsizetype(expectedBytes));
        return true;
    }

    void discardActiveScan()
    {
        if (m_active) ++m_discardedScanCount;
        reset();
        m_lastChunk.reset();
    }

    ScanAssemblyResult completeDirect(const std::shared_ptr<CameraFrame> &chunk)
    {
        if (chunk->data.size() > m_maximumPayloadBytes) {
            return reject(QStringLiteral("complete scan payload exceeds assembly memory limit"));
        }
        auto completed = std::make_shared<CameraFrame>(*chunk);
        completed->frameNumber = ++m_completedScanCount;
        completed->expectedScanRows = m_expectedRows;
        completed->accumulatedScanRows = chunk->height;
        completed->scanFragmentCount = 1;
        completed->discardedScanCount = m_discardedScanCount;
        completed->completeScan = true;
        return {ScanAssemblyOutcome::Complete, std::move(completed), {}};
    }

    ScanAssemblyResult reject(const QString &error)
    {
        return {ScanAssemblyOutcome::Rejected, {}, error};
    }

    std::uint32_t m_expectedRows = 0;
    qsizetype m_maximumPayloadBytes = kDefaultMaximumPayloadBytes;
    std::uint32_t m_width = 0;
    std::uint32_t m_accumulatedRows = 0;
    std::uint32_t m_nextFrameNumber = 0;
    std::uint32_t m_fragmentCount = 0;
    std::uint32_t m_completedScanCount = 0;
    std::uint32_t m_discardedScanCount = 0;
    bool m_active = false;
    QByteArray m_data;
    std::shared_ptr<CameraFrame> m_lastChunk;
};

} // namespace pcv::camera::detail