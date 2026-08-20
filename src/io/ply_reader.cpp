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
#include <limits>
#include <numeric>

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

QVector<AsciiChunk> splitAsciiChunks(const char *begin, const char *end,
                                     qsizetype pointCount, int requestedChunks,
                                     QString *error) {
    QVector<AsciiChunk> chunks;
    if (!begin || !end || end < begin || pointCount <= 0) {
        if (error) *error = QStringLiteral("ASCII 顶点数据区无效");
        return chunks;
    }
    const int chunkCount = qBound(1, requestedChunks,
                                  int(qMin<qsizetype>(pointCount, 64)));
    QVector<const char *> starts(chunkCount + 1, end);
    starts[0] = begin;
    for (int i = 1; i < chunkCount; ++i) {
        const qsizetype offset = qsizetype((end - begin) * qint64(i) / chunkCount);
        const char *cursor = begin + offset;
        while (cursor < end && cursor > begin && cursor[-1] != '\n') ++cursor;
        starts[i] = cursor;
    }
    starts[chunkCount] = end;
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
                     int lastRequiredIndex, pointcloud::Point3D *point) {
    const char *cursor = line.constData();
    const char *end = cursor + line.size();
    float values[6] = {};
    for (int column = 0; column <= lastRequiredIndex; ++column) {
        float value = 0.0f;
        if (!parseFloatFast(cursor, end, &value)) return false;
        for (int component = 0; component < 6; ++component)
            if (column == indices[component]) values[component] = value;
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
        const char *mappedCursor = mappedBegin;
        const char *mappedEnd = mappedBegin ? mappedBegin + payloadBytes : nullptr;
        std::array<char, 64 * 1024> buffer{};
        QVector<AsciiChunk> chunks;
        if (mapped) {
            QElapsedTimer boundaryTimer;
            boundaryTimer.start();
            QString boundaryError;
            chunks = splitAsciiChunks(mappedBegin, mappedEnd,
                                      result.declaredPointCount,
                                      4, &boundaryError);
            result.boundaryScanElapsedMs = boundaryTimer.elapsed();
            if (chunks.isEmpty()) {
                file.unmap(mapped);
                result.error = boundaryError;
                return result;
            }
        }
        bool parseFailed = false;
        for (const AsciiChunk &chunk : chunks.isEmpty()
                 ? QVector<AsciiChunk>{AsciiChunk{mappedBegin, mappedEnd, 0,
                                                  result.declaredPointCount}}
                 : chunks) {
            const qsizetype chunkEnd = chunk.firstPoint + chunk.pointCount;
            for (qsizetype index = chunk.firstPoint; index < chunkEnd; ++index) {
            if (cancelled(index)) {
                result.cancelled = true;
                result.error = QStringLiteral("已取消");
                if (mapped) file.unmap(mapped);
                return result;
            }
            QByteArray line;
            if (mapped) {
                mappedCursor = (index == chunk.firstPoint) ? chunk.begin : mappedCursor;
                if (mappedCursor >= chunk.end) { parseFailed = true; break; }
                const char *lineEnd = static_cast<const char *>(std::memchr(
                    mappedCursor, '\n', size_t(chunk.end - mappedCursor)));
                lineEnd = lineEnd ? lineEnd + 1 : chunk.end;
                line = QByteArray::fromRawData(mappedCursor, int(lineEnd - mappedCursor));
                mappedCursor = lineEnd;
            } else {
                const qint64 length = file.readLine(buffer.data(), buffer.size());
                if (length <= 0) { parseFailed = true; break; }
                line = QByteArray::fromRawData(buffer.data(), int(length));
            }
            pointcloud::Point3D point;
            if (!parseAsciiPoint(line, indices, lastRequiredIndex, &point)) {
                parseFailed = true;
                break;
            }
            result.points[parsedCount++] = point;
            updateBounds(point);
            reportProgress(index + 1);
            }
            if (parseFailed) break;
        }
        if (mapped) file.unmap(mapped);
        if (parseFailed && parsedCount == result.declaredPointCount)
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
        const QByteArray payload = file.read(payloadSize);
        const char *cursor = payload.constData();
        const char *end = cursor + payload.size();
        bool ok = payload.size() == payloadSize;
        const bool bigEndian = result.format == PlyFormat::BinaryBigEndian;
        for (qsizetype index = 0; index < result.declaredPointCount && ok; ++index) {
            if (cancelled(index)) {
                result.cancelled = true;
                result.error = QStringLiteral("已取消");
                return result;
            }
            pointcloud::Point3D point;
            for (int column = 0; column < properties.size(); ++column) {
                const double value = readBinaryScalar(cursor, end, properties[column].type,
                                                      bigEndian, &ok);
                if (column == indices[0]) point.x = float(value);
                else if (column == indices[1]) point.y = float(value);
                else if (column == indices[2]) point.z = float(value);
                else if (column == indices[3]) point.nx = float(value);
                else if (column == indices[4]) point.ny = float(value);
                else if (column == indices[5]) point.nz = float(value);
            }
            if (ok) result.points[parsedCount++] = point;
            if (ok) updateBounds(point);
            reportProgress(index + 1);
        }
    }

    if (parsedCount != result.declaredPointCount) {
        const qsizetype actual = parsedCount;
        result.points.clear();
        result.error = QStringLiteral("PLY 顶点数据不完整：声明 %1 点，实际读取 %2 点")
            .arg(result.declaredPointCount).arg(actual);
        return result;
    }
    result.ok = true;
    result.parseElapsedMs = parseTimer.elapsed();
    result.totalElapsedMs = totalTimer.elapsed();
    return result;
}

} // namespace pcv::detail::io
