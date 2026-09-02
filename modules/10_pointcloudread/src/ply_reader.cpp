#include <pcv/io/ply_reader.h>

#include <QFile>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <future>
#include <limits>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace {

struct Property {
    QByteArray type;
    QByteArray name;
};

struct AsciiChunk {
    const char *begin = nullptr;
    const char *end = nullptr;
    qsizetype firstPoint = 0;
    qsizetype pointCount = 0;
};

struct ChunkResult {
    qsizetype parsedCount = 0;
    pointcloud::Point3D minimum;
    pointcloud::Point3D maximum;
    bool hasBounds = false;
    QString error;
};

int selectWorkerCount(qsizetype pointCount, qint64 payloadBytes,
                      int requestedWorkers) {
    if (pointCount <= 1) return 1;
    if (requestedWorkers > 0)
        return qBound(1, requestedWorkers, int(qMin<qsizetype>(pointCount, 8)));

    const unsigned int reportedHardware = std::thread::hardware_concurrency();
    const int hardwareLimit = reportedHardware > 0
        ? qBound(1, int(reportedHardware), 4) : 2;
    // Thread spawn overhead is tens of microseconds; below these thresholds a
    // parallel decode would not pay off. Above them the workload is large
    // enough to saturate all four cores.
    if (pointCount < 20000 || payloadBytes < 1024 * 1024)
        return 1;
    if (pointCount < 100000 || payloadBytes < 4 * 1024 * 1024)
        return qMin(2, hardwareLimit);
    return hardwareLimit;
}

static inline float decodeBinaryFloat(const char *data, bool bigEndian) {
    const uchar *bytes = reinterpret_cast<const uchar *>(data);
    const quint32 raw = bigEndian ? qFromBigEndian<quint32>(bytes)
                                  : qFromLittleEndian<quint32>(bytes);
    float value;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

bool isFloat32Type(const QByteArray &type) {
    return type == "float" || type == "float32";
}

QVector<AsciiChunk> splitAsciiChunks(const char *begin, const char *end,
                                     qsizetype pointCount, int requestedChunks,
                                     QString *error) {
    QVector<AsciiChunk> chunks;
    if (!begin || !end || end < begin || pointCount <= 0) {
        if (error) *error = QStringLiteral("ASCII 顶点数据区无效");
        return chunks;
    }
    const char *vertexEnd = begin;
    for (qsizetype line = 0; line < pointCount; ++line) {
        if (vertexEnd >= end) {
            if (error) *error = QStringLiteral("ASCII 顶点数据不完整：声明 %1 点，扫描 %2 点")
                .arg(pointCount).arg(line);
            return chunks;
        }
        const char *lineEnd = static_cast<const char *>(std::memchr(
            vertexEnd, '\n', size_t(end - vertexEnd)));
        vertexEnd = lineEnd ? lineEnd + 1 : end;
    }
    const int chunkCount = qBound(1, requestedChunks,
                                  int(qMin<qsizetype>(pointCount, 64)));
    QVector<const char *> starts(chunkCount + 1, vertexEnd);
    starts[0] = begin;
    for (int i = 1; i < chunkCount; ++i) {
        const qsizetype offset = qsizetype((vertexEnd - begin) * qint64(i) / chunkCount);
        const char *cursor = begin + offset;
        while (cursor < vertexEnd && cursor > begin && cursor[-1] != '\n') ++cursor;
        starts[i] = cursor;
    }
    starts[chunkCount] = vertexEnd;
    qsizetype firstPoint = 0;
    for (int i = 0; i < chunkCount; ++i) {
        AsciiChunk chunk;
        chunk.begin = starts[i];
        chunk.end = starts[i + 1];
        chunk.firstPoint = firstPoint;
        const char *cursor = chunk.begin;
        while (cursor < chunk.end) {
            const char *lineEnd = static_cast<const char *>(std::memchr(
                cursor, '\n', size_t(chunk.end - cursor)));
            cursor = lineEnd ? lineEnd + 1 : chunk.end;
            ++chunk.pointCount;
        }
        firstPoint += chunk.pointCount;
        chunks.push_back(chunk);
    }
    if (firstPoint != pointCount) {
        chunks.clear();
        if (error) *error = QStringLiteral("ASCII 分块边界校验失败：声明 %1 点，扫描 %2 点")
            .arg(pointCount).arg(firstPoint);
    }
    return chunks;
}

int scalarBytes(const QByteArray &type) {
    if (type == "char" || type == "int8" || type == "uchar" || type == "uint8") return 1;
    if (type == "short" || type == "int16" || type == "ushort" || type == "uint16") return 2;
    if (type == "int" || type == "int32" || type == "uint" || type == "uint32"
        || type == "float" || type == "float32") return 4;
    if (type == "double" || type == "float64") return 8;
    return 0;
}

// Integer kind of a supported scalar type, resolved once outside the hot
// loop so per-point decode never compares property type strings.
int scalarKind(const QByteArray &type) {
    if (type == "float" || type == "float32") return 0;
    if (type == "double" || type == "float64") return 1;
    if (type == "uchar" || type == "uint8") return 2;
    if (type == "char" || type == "int8") return 3;
    if (type == "ushort" || type == "uint16") return 4;
    if (type == "short" || type == "int16") return 5;
    if (type == "uint" || type == "uint32") return 6;
    if (type == "int" || type == "int32") return 7;
    return -1;
}

// Fixed-stride decode of any supported scalar kind to float. The kind switch
// compiles to a jump table in optimized builds.
float decodeBinaryScalarKind(const char *data, int kind, bool bigEndian) {
    const uchar *d = reinterpret_cast<const uchar *>(data);
    switch (kind) {
    case 0: { // float32
        const quint32 raw = bigEndian ? qFromBigEndian<quint32>(d)
                                      : qFromLittleEndian<quint32>(d);
        float value;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }
    case 1: { // float64
        const quint64 raw = bigEndian ? qFromBigEndian<quint64>(d)
                                      : qFromLittleEndian<quint64>(d);
        double value;
        std::memcpy(&value, &raw, sizeof(value));
        return float(value);
    }
    case 2: return float(d[0]);                              // uchar/uint8
    case 3: return float(qint8(d[0]));                       // char/int8
    case 4: {                                                // ushort/uint16
        const quint16 raw = bigEndian ? qFromBigEndian<quint16>(d)
                                      : qFromLittleEndian<quint16>(d);
        return float(raw);
    }
    case 5: {                                                // short/int16
        const quint16 raw = bigEndian ? qFromBigEndian<quint16>(d)
                                      : qFromLittleEndian<quint16>(d);
        return float(qint16(raw));
    }
    case 6: {                                                // uint/uint32
        const quint32 raw = bigEndian ? qFromBigEndian<quint32>(d)
                                      : qFromLittleEndian<quint32>(d);
        return float(raw);
    }
    case 7: {                                                // int/int32
        const quint32 raw = bigEndian ? qFromBigEndian<quint32>(d)
                                      : qFromLittleEndian<quint32>(d);
        return float(qint32(raw));
    }
    default:
        return 0.0f;
    }
}

bool parseFloatFast(const char *&cursor, const char *end, float *value) {
    while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (cursor >= end) return false;
    int sign = 1;
    if (*cursor == '+' || *cursor == '-') {
        if (*cursor++ == '-') sign = -1;
    }
    double mantissa = 0.0;
    int digits = 0;
    while (cursor < end && *cursor >= '0' && *cursor <= '9') {
        mantissa = mantissa * 10.0 + double(*cursor++ - '0');
        ++digits;
    }
    if (cursor < end && *cursor == '.') {
        ++cursor;
        double scale = 0.1;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') {
            mantissa += double(*cursor++ - '0') * scale;
            scale *= 0.1;
            ++digits;
        }
    }
    if (digits == 0) return false;
    int exponent = 0;
    if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        int exponentSign = 1;
        if (cursor < end && (*cursor == '+' || *cursor == '-')) {
            if (*cursor++ == '-') exponentSign = -1;
        }
        int exponentDigits = 0;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') {
            exponent = qMin( exponent * 10 + int(*cursor++ - '0'), 4000);
            ++exponentDigits;
        }
        if (exponentDigits == 0) return false;
        exponent *= exponentSign;
    }
    const double converted = double(sign) * mantissa * std::pow(10.0, exponent);
    if (!std::isfinite(converted) || std::abs(converted) > std::numeric_limits<float>::max())
        return false;
    *value = float(converted);
    return std::isfinite(*value);
}

bool parseAsciiPoint(const QByteArray &line, const int indices[6],
                     int lastRequiredIndex, pointcloud::Point3D *point,
                     int extraIndex = -1, float *extraValue = nullptr) {
    const char *cursor = line.constData();
    const char *end = cursor + line.size();
    const int parseLimit = std::max(lastRequiredIndex, extraIndex);
    float values[6] = {};
    for (int column = 0; column <= parseLimit; ++column) {
        float value = 0.0f;
        if (!parseFloatFast(cursor, end, &value)) return false;
        for (int component = 0; component < 6; ++component)
            if (column == indices[component]) values[component] = value;
        if (extraValue && column == extraIndex) *extraValue = value;
    }
    *point = {values[0], values[1], values[2], values[3], values[4], values[5]};
    return true;
}

double readBinaryScalar(const char *&cursor, const char *end,
                        const QByteArray &type, bool bigEndian, bool *ok) {
    const int bytes = scalarBytes(type);
    if (bytes <= 0 || cursor + bytes > end) {
        *ok = false;
        return 0.0;
    }
    const uchar *data = reinterpret_cast<const uchar *>(cursor);
    cursor += bytes;
    if (type == "float" || type == "float32") {
        const quint32 raw = bigEndian ? qFromBigEndian<quint32>(data)
                                      : qFromLittleEndian<quint32>(data);
        float value;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }
    if (type == "double" || type == "float64") {
        const quint64 raw = bigEndian ? qFromBigEndian<quint64>(data)
                                      : qFromLittleEndian<quint64>(data);
        double value;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }
    if (type == "uchar" || type == "uint8") return data[0];
    if (type == "char" || type == "int8") return qint8(data[0]);
    if (type == "ushort" || type == "uint16")
        return bigEndian ? qFromBigEndian<quint16>(data) : qFromLittleEndian<quint16>(data);
    if (type == "short" || type == "int16") {
        const quint16 raw = bigEndian ? qFromBigEndian<quint16>(data)
                                      : qFromLittleEndian<quint16>(data);
        return qint16(raw);
    }
    const quint32 raw = bigEndian ? qFromBigEndian<quint32>(data)
                                  : qFromLittleEndian<quint32>(data);
    return type == "uint" || type == "uint32" ? raw : qint32(raw);
}

} // namespace

namespace pcv::detail::io {

PlyReadResult readPly(const QString &fileName, const PlyReadOptions &options) {
    PlyReadResult result;
    QElapsedTimer totalTimer;
    totalTimer.start();
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.readLine().trimmed() != "ply") {
        result.error = QStringLiteral("不是 PLY 文件");
        return result;
    }

    QElapsedTimer headerTimer;
    headerTimer.start();
    bool inVertex = false;
    bool headerEnded = false;
    bool unsupportedVertexList = false;
    QVector<Property> properties;
    while (!file.atEnd()) {
        const QByteArray text = file.readLine().trimmed();
        const QStringList fields = QString::fromLatin1(text).split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (fields.isEmpty()) continue;
        if (fields[0] == QStringLiteral("format")) {
            const QString format = fields.value(1);
            if (format == QStringLiteral("ascii")) result.format = PlyFormat::Ascii;
            else if (format == QStringLiteral("binary_little_endian"))
                result.format = PlyFormat::BinaryLittleEndian;
            else if (format == QStringLiteral("binary_big_endian"))
                result.format = PlyFormat::BinaryBigEndian;
            else {
                result.error = QStringLiteral("不支持的 PLY 格式：%1").arg(format);
                return result;
            }
        } else if (fields[0] == QStringLiteral("element")) {
            inVertex = fields.value(1) == QStringLiteral("vertex");
            if (inVertex) result.declaredPointCount = fields.value(2).toLongLong();
        } else if (inVertex && fields[0] == QStringLiteral("property")) {
            if (fields.value(1) == QStringLiteral("list")) {
                unsupportedVertexList = true;
            } else if (fields.size() >= 3) {
                properties.push_back({fields[1].toLatin1(), fields[2].toLatin1()});
            }
        } else if (fields[0] == QStringLiteral("end_header")) {
            headerEnded = true;
            break;
        }
    }

    if (!headerEnded) result.error = QStringLiteral("PLY 文件头不完整，缺少 end_header");
    else if (unsupportedVertexList) result.error = QStringLiteral("暂不支持 vertex 元素中的 list 属性");
    else if (result.declaredPointCount <= 0) result.error = QStringLiteral("PLY 文件缺少有效的 vertex 定义");
    if (!result.error.isEmpty()) return result;
    result.headerElapsedMs = headerTimer.elapsed();
    QElapsedTimer parseTimer;
    parseTimer.start();

    int indices[6] = {-1, -1, -1, -1, -1, -1};
    for (int index = 0; index < properties.size(); ++index) {
        if (scalarBytes(properties[index].type) == 0) {
            result.error = QStringLiteral("不支持的 PLY 属性类型：%1")
                .arg(QString::fromLatin1(properties[index].type));
            return result;
        }
        const QByteArray name = properties[index].name.toLower();
        if (name == "x") indices[0] = index;
        else if (name == "y") indices[1] = index;
        else if (name == "z") indices[2] = index;
        else if (name == "nx") indices[3] = index;
        else if (name == "ny") indices[4] = index;
        else if (name == "nz") indices[5] = index;
    }
    if (indices[0] < 0 || indices[1] < 0 || indices[2] < 0) {
        result.error = QStringLiteral("PLY 文件缺少 x/y/z 顶点属性");
        return result;
    }
    const int lastRequiredIndex = std::max({indices[0], indices[1], indices[2],
                                            indices[3], indices[4], indices[5]});
    if (result.declaredPointCount > std::numeric_limits<int>::max()) {
        result.error = QStringLiteral("PLY 顶点数量超过当前实现限制");
        return result;
    }

    int intensityIndex = -1;
    if (options.readIntensity) {
        for (int index = 0; index < properties.size(); ++index) {
            if (properties[index].name.toLower() == "intensity") {
                intensityIndex = index;
                break;
            }
        }
    }
    const bool hasIntensityOutput = options.readIntensity && intensityIndex >= 0;
    if (hasIntensityOutput) result.intensity.resize(result.declaredPointCount);

    result.points.resize(result.declaredPointCount);
    qsizetype parsedCount = 0;
    auto updateBounds = [&result](const pointcloud::Point3D &point) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
        if (!result.hasBounds) {
            result.minimum = result.maximum = point;
            result.hasBounds = true;
            return;
        }
        result.minimum.x = std::min(result.minimum.x, point.x);
        result.minimum.y = std::min(result.minimum.y, point.y);
        result.minimum.z = std::min(result.minimum.z, point.z);
        result.maximum.x = std::max(result.maximum.x, point.x);
        result.maximum.y = std::max(result.maximum.y, point.y);
        result.maximum.z = std::max(result.maximum.z, point.z);
    };
    const auto reportProgress = [&](qsizetype index) {
        if (options.progress && ((index % 10000) == 0 || index == result.declaredPointCount))
            options.progress(index, result.declaredPointCount);
    };
    const auto cancelled = [&](qsizetype index) {
        return (index % 10000) == 0 && options.isCancelled && options.isCancelled();
    };

    if (result.format == PlyFormat::Ascii) {
        const qint64 payloadOffset = file.pos();
        const qint64 payloadBytes = file.size() - payloadOffset;
        uchar *mapped = payloadBytes > 0 ? file.map(payloadOffset, payloadBytes) : nullptr;
        const char *mappedBegin = reinterpret_cast<const char *>(mapped);
        const char *mappedEnd = mappedBegin ? mappedBegin + payloadBytes : nullptr;
        std::array<char, 64 * 1024> buffer{};
        QVector<AsciiChunk> chunks;
        if (mapped) {
            QElapsedTimer boundaryTimer;
            boundaryTimer.start();
            QString boundaryError;
            result.asciiWorkerCount = selectWorkerCount(
                result.declaredPointCount, payloadBytes, options.asciiWorkerCount);
            chunks = splitAsciiChunks(mappedBegin, mappedEnd,
                                      result.declaredPointCount,
                                      result.asciiWorkerCount, &boundaryError);
            result.boundaryScanElapsedMs = boundaryTimer.elapsed();
            if (chunks.isEmpty()) {
                file.unmap(mapped);
                result.error = boundaryError;
                return result;
            }
        }
        bool parseFailed = false;
        if (mapped) {
            pointcloud::Point3D *output = result.points.data();
            std::atomic<qsizetype> completed{0};
            std::atomic_bool stop{false};
            std::mutex callbackMutex;
            const auto parseChunk = [&](const AsciiChunk &chunk) {
                ChunkResult local;
                const char *cursor = chunk.begin;
                for (qsizetype localIndex = 0; localIndex < chunk.pointCount; ++localIndex) {
                    if (stop.load(std::memory_order_relaxed)) break;
                    if ((localIndex % 10000) == 0 && options.isCancelled) {
                        std::lock_guard<std::mutex> lock(callbackMutex);
                        if (options.isCancelled()) {
                            stop.store(true, std::memory_order_relaxed);
                            local.error = QStringLiteral("已取消");
                            break;
                        }
                    }
                    if (cursor >= chunk.end) {
                        local.error = QStringLiteral("Chunk %1 在顶点 %2 前提前结束")
                            .arg(chunk.firstPoint).arg(chunk.firstPoint + localIndex);
                        stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                    const char *lineEnd = static_cast<const char *>(std::memchr(
                        cursor, '\n', size_t(chunk.end - cursor)));
                    lineEnd = lineEnd ? lineEnd + 1 : chunk.end;
                    pointcloud::Point3D point;
                    float extraValue = 0.0f;
                    if (!parseAsciiPoint(QByteArray::fromRawData(
                                             cursor, int(lineEnd - cursor)),
                                         indices, lastRequiredIndex, &point,
                                         hasIntensityOutput ? intensityIndex : -1,
                                         hasIntensityOutput ? &extraValue : nullptr)) {
                        local.error = QStringLiteral("ASCII 顶点 %1 解析失败")
                            .arg(chunk.firstPoint + localIndex);
                        stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                    output[chunk.firstPoint + localIndex] = point;
                    if (hasIntensityOutput)
                        result.intensity[chunk.firstPoint + localIndex] = extraValue;
                    if (!local.hasBounds) {
                        local.minimum = local.maximum = point;
                        local.hasBounds = true;
                    } else {
                        local.minimum.x = std::min(local.minimum.x, point.x);
                        local.minimum.y = std::min(local.minimum.y, point.y);
                        local.minimum.z = std::min(local.minimum.z, point.z);
                        local.maximum.x = std::max(local.maximum.x, point.x);
                        local.maximum.y = std::max(local.maximum.y, point.y);
                        local.maximum.z = std::max(local.maximum.z, point.z);
                    }
                    ++local.parsedCount;
                    cursor = lineEnd;
                    if (options.progress) {
                        const qsizetype done =
                            completed.fetch_add(1, std::memory_order_relaxed) + 1;
                        if ((done % 10000) == 0 || done == result.declaredPointCount) {
                            std::lock_guard<std::mutex> lock(callbackMutex);
                            options.progress(done, result.declaredPointCount);
                        }
                    }
                }
                return local;
            };
            std::vector<std::future<ChunkResult>> futures;
            futures.reserve(chunks.size());
            for (const AsciiChunk &chunk : chunks)
                futures.push_back(std::async(std::launch::async, parseChunk, chunk));
            for (auto &future : futures) {
                ChunkResult local = future.get();
                parsedCount += local.parsedCount;
                if (!local.error.isEmpty() && result.error.isEmpty()) result.error = local.error;
                if (local.hasBounds) {
                    updateBounds(local.minimum);
                    updateBounds(local.maximum);
                }
            }
            result.cancelled = result.error == QStringLiteral("已取消");
            parseFailed = !result.error.isEmpty();
        } else {
            result.asciiWorkerCount = 1;
            for (qsizetype index = 0; index < result.declaredPointCount; ++index) {
                if (cancelled(index)) {
                    result.cancelled = true;
                    result.error = QStringLiteral("已取消");
                    return result;
                }
                const qint64 length = file.readLine(buffer.data(), buffer.size());
                if (length <= 0) { parseFailed = true; break; }
                pointcloud::Point3D point;
                float extraValue = 0.0f;
                if (!parseAsciiPoint(QByteArray::fromRawData(buffer.data(), int(length)),
                                     indices, lastRequiredIndex, &point,
                                     hasIntensityOutput ? intensityIndex : -1,
                                     hasIntensityOutput ? &extraValue : nullptr)) {
                    parseFailed = true;
                    break;
                }
                result.points[parsedCount] = point;
                if (hasIntensityOutput) result.intensity[parsedCount] = extraValue;
                ++parsedCount;
                updateBounds(point);
                reportProgress(index + 1);
            }
        }
        if (mapped) file.unmap(mapped);
        if (parseFailed && result.error.isEmpty())
            result.error = QStringLiteral("ASCII 分块解析失败");
    } else {
        const qsizetype bytesPerVertex = std::accumulate(
            properties.cbegin(), properties.cend(), qsizetype(0),
            [](qsizetype total, const Property &property) {
                return total + scalarBytes(property.type);
            });
        if (bytesPerVertex <= 0
            || result.declaredPointCount > std::numeric_limits<qsizetype>::max() / bytesPerVertex) {
            result.error = QStringLiteral("PLY 二进制顶点数据大小溢出");
            return result;
        }
        const qsizetype payloadSize = bytesPerVertex * result.declaredPointCount;
        const bool bigEndian = result.format == PlyFormat::BinaryBigEndian;
        const bool fixedWidthStride = std::all_of(
            properties.cbegin(), properties.cend(),
            [](const Property &property) { return scalarBytes(property.type) > 0; });
        const bool floatXyz = isFloat32Type(properties[indices[0]].type)
            && isFloat32Type(properties[indices[1]].type)
            && isFloat32Type(properties[indices[2]].type);
        QVector<qsizetype> columnOffsets;
        columnOffsets.reserve(properties.size());
        {
            qsizetype runningOffset = 0;
            for (const Property &property : properties) {
                columnOffsets.push_back(runningOffset);
                runningOffset += scalarBytes(property.type);
            }
        }
        if (fixedWidthStride) {
            // Parallel fixed-stride decode. The payload comes from mmap when
            // possible, otherwise from one owned read; both decode the same
            // way, so mmap failure no longer degrades to serial parsing.
            const qsizetype xOffset = columnOffsets[indices[0]];
            const qsizetype yOffset = columnOffsets[indices[1]];
            const qsizetype zOffset = columnOffsets[indices[2]];
            const int xKind = scalarKind(properties[indices[0]].type);
            const int yKind = scalarKind(properties[indices[1]].type);
            const int zKind = scalarKind(properties[indices[2]].type);
            // Contiguous float32 x,y,z in little-endian files decode directly
            // as a float[3] block, avoiding per-column call overhead. The
            // stride must be 4-byte aligned so the block read stays aligned.
            const bool contiguousXyz = floatXyz && !bigEndian
                && (bytesPerVertex % 4) == 0
                && yOffset == xOffset + 4 && zOffset == xOffset + 8;
            const int intensityKind = hasIntensityOutput
                ? scalarKind(properties[intensityIndex].type) : -1;
            const qsizetype intensityOffset = hasIntensityOutput
                ? columnOffsets[intensityIndex] : 0;
            // Normals are decoded too when the file declares them.
            const bool hasNx = indices[3] >= 0;
            const bool hasNy = indices[4] >= 0;
            const bool hasNz = indices[5] >= 0;
            const int nxKind = hasNx ? scalarKind(properties[indices[3]].type) : -1;
            const int nyKind = hasNy ? scalarKind(properties[indices[4]].type) : -1;
            const int nzKind = hasNz ? scalarKind(properties[indices[5]].type) : -1;
            const qsizetype nxOffset = hasNx ? columnOffsets[indices[3]] : 0;
            const qsizetype nyOffset = hasNy ? columnOffsets[indices[4]] : 0;
            const qsizetype nzOffset = hasNz ? columnOffsets[indices[5]] : 0;

            const qint64 payloadOffset = file.pos();
            uchar *mapped = payloadSize > 0
                ? file.map(payloadOffset, payloadSize) : nullptr;
            QByteArray ownedBuffer;
            const char *payload = nullptr;
            if (mapped) {
                payload = reinterpret_cast<const char *>(mapped);
            } else {
                ownedBuffer = file.read(payloadSize);
                payload = ownedBuffer.constData();
                if (ownedBuffer.size() != payloadSize) {
                    result.error = QStringLiteral("PLY 二进制顶点数据不完整");
                    return result;
                }
            }
            result.fastPath = true;
            const int binaryWorkerCount = selectWorkerCount(
                result.declaredPointCount, payloadSize, options.binaryWorkerCount);
            result.binaryWorkerCount = binaryWorkerCount;
            pointcloud::Point3D *output = result.points.data();
            std::atomic<qsizetype> completed{0};
            std::atomic_bool stop{false};
            std::mutex callbackMutex;
            const auto parseBinaryChunk = [&](qsizetype firstPoint, qsizetype pointCount) {
                ChunkResult local;
                for (qsizetype localIndex = 0; localIndex < pointCount; ++localIndex) {
                    if (stop.load(std::memory_order_relaxed)) break;
                    if ((localIndex % 10000) == 0 && options.isCancelled) {
                        std::lock_guard<std::mutex> lock(callbackMutex);
                        if (options.isCancelled()) {
                            stop.store(true, std::memory_order_relaxed);
                            local.error = QStringLiteral("已取消");
                            break;
                        }
                    }
                    const qsizetype pointIndex = firstPoint + localIndex;
                    const char *source = payload + pointIndex * bytesPerVertex;
                    pointcloud::Point3D point;
                    if (contiguousXyz) {
                        const float *pf = reinterpret_cast<const float *>(source + xOffset);
                        point.x = pf[0];
                        point.y = pf[1];
                        point.z = pf[2];
                    } else if (floatXyz) {
                        point.x = decodeBinaryFloat(source + xOffset, bigEndian);
                        point.y = decodeBinaryFloat(source + yOffset, bigEndian);
                        point.z = decodeBinaryFloat(source + zOffset, bigEndian);
                    } else {
                        point.x = decodeBinaryScalarKind(source + xOffset, xKind, bigEndian);
                        point.y = decodeBinaryScalarKind(source + yOffset, yKind, bigEndian);
                        point.z = decodeBinaryScalarKind(source + zOffset, zKind, bigEndian);
                    }
                    if (hasNx) point.nx = decodeBinaryScalarKind(source + nxOffset, nxKind, bigEndian);
                    if (hasNy) point.ny = decodeBinaryScalarKind(source + nyOffset, nyKind, bigEndian);
                    if (hasNz) point.nz = decodeBinaryScalarKind(source + nzOffset, nzKind, bigEndian);
                    output[pointIndex] = point;
                    if (hasIntensityOutput)
                        result.intensity[pointIndex] = decodeBinaryScalarKind(
                            source + intensityOffset, intensityKind, bigEndian);
                    if (std::isfinite(point.x) && std::isfinite(point.y)
                        && std::isfinite(point.z)) {
                        if (!local.hasBounds) {
                            local.minimum = local.maximum = point;
                            local.hasBounds = true;
                        } else {
                            local.minimum.x = std::min(local.minimum.x, point.x);
                            local.minimum.y = std::min(local.minimum.y, point.y);
                            local.minimum.z = std::min(local.minimum.z, point.z);
                            local.maximum.x = std::max(local.maximum.x, point.x);
                            local.maximum.y = std::max(local.maximum.y, point.y);
                            local.maximum.z = std::max(local.maximum.z, point.z);
                        }
                    }
                    const qsizetype localDone = ++local.parsedCount;
                    // Batch the atomic progress counter: one RMW per 10k
                    // points instead of per point; the tail is flushed after
                    // the chunk loop.
                    if ((localDone % 10000) == 0) {
                        const qsizetype done =
                            completed.fetch_add(10000, std::memory_order_relaxed) + 10000;
                        if (options.progress) {
                            std::lock_guard<std::mutex> lock(callbackMutex);
                            options.progress(done, result.declaredPointCount);
                        }
                    }
                }
                const qsizetype remainder = local.parsedCount % 10000;
                if (remainder != 0)
                    completed.fetch_add(remainder, std::memory_order_relaxed);
                return local;
            };
            std::vector<std::future<ChunkResult>> futures;
            futures.reserve(size_t(binaryWorkerCount));
            qsizetype firstPoint = 0;
            for (int worker = 0; worker < binaryWorkerCount; ++worker) {
                const qsizetype nextPoint = result.declaredPointCount
                    * qsizetype(worker + 1) / binaryWorkerCount;
                futures.push_back(std::async(std::launch::async, parseBinaryChunk,
                                             firstPoint, nextPoint - firstPoint));
                firstPoint = nextPoint;
            }
            for (auto &future : futures) {
                ChunkResult local = future.get();
                parsedCount += local.parsedCount;
                if (!local.error.isEmpty() && result.error.isEmpty()) result.error = local.error;
                if (local.hasBounds) {
                    updateBounds(local.minimum);
                    updateBounds(local.maximum);
                }
            }
            if (options.progress && result.error.isEmpty())
                options.progress(result.declaredPointCount, result.declaredPointCount);
            result.cancelled = result.error == QStringLiteral("已取消");
            if (mapped) file.unmap(mapped);
        } else {
            result.fastPath = false;
            result.binaryWorkerCount = 1;
            const QByteArray payload = file.read(payloadSize);
            const char *cursor = payload.constData();
            const char *end = cursor + payload.size();
            bool ok = payload.size() == payloadSize;
            for (qsizetype index = 0; index < result.declaredPointCount && ok; ++index) {
                if (cancelled(index)) {
                    result.cancelled = true;
                    result.error = QStringLiteral("已取消");
                    return result;
                }
                pointcloud::Point3D point;
                float pointIntensity = 0.0f;
                for (int column = 0; column < properties.size(); ++column) {
                    const double value = readBinaryScalar(cursor, end, properties[column].type,
                                                          bigEndian, &ok);
                    if (column == indices[0]) point.x = float(value);
                    else if (column == indices[1]) point.y = float(value);
                    else if (column == indices[2]) point.z = float(value);
                    else if (column == indices[3]) point.nx = float(value);
                    else if (column == indices[4]) point.ny = float(value);
                    else if (column == indices[5]) point.nz = float(value);
                    else if (hasIntensityOutput && column == intensityIndex)
                        pointIntensity = float(value);
                }
                if (ok) {
                    result.points[parsedCount] = point;
                    if (hasIntensityOutput) result.intensity[parsedCount] = pointIntensity;
                    ++parsedCount;
                }
                if (ok) updateBounds(point);
                reportProgress(index + 1);
            }
        }
    }

    if (parsedCount != result.declaredPointCount) {
        const qsizetype actual = parsedCount;
        result.points.clear();
        result.intensity.clear();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("PLY 顶点数据不完整：声明 %1 点，实际读取 %2 点")
                .arg(result.declaredPointCount).arg(actual);
        }
        return result;
    }
    result.ok = true;
    result.parseElapsedMs = parseTimer.elapsed();
    result.totalElapsedMs = totalTimer.elapsed();
    return result;
}

} // namespace pcv::detail::io
