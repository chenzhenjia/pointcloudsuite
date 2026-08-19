#include <pcv/io/ply_reader.h>

#include <QFile>
#include <QRegularExpression>
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

int scalarBytes(const QByteArray &type) {
    if (type == "char" || type == "int8" || type == "uchar" || type == "uint8") return 1;
    if (type == "short" || type == "int16" || type == "ushort" || type == "uint16") return 2;
    if (type == "int" || type == "int32" || type == "uint" || type == "uint32"
        || type == "float" || type == "float32") return 4;
    if (type == "double" || type == "float64") return 8;
    return 0;
}

bool parseAsciiPoint(const QByteArray &line, const int indices[6],
                     pointcloud::Point3D *point) {
    const int last = std::max({indices[0], indices[1], indices[2],
                               indices[3], indices[4], indices[5]});
    const char *cursor = line.constData();
    const char *end = cursor + line.size();
    float values[6] = {};
    for (int column = 0; column <= last; ++column) {
        while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (cursor >= end) return false;
        char *next = nullptr;
        const float value = std::strtof(cursor, &next);
        if (next == cursor) return false;
        for (int component = 0; component < 6; ++component)
            if (column == indices[component]) values[component] = value;
        cursor = next;
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
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.readLine().trimmed() != "ply") {
        result.error = QStringLiteral("不是 PLY 文件");
        return result;
    }

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
        std::array<char, 64 * 1024> buffer{};
        for (qsizetype index = 0; index < result.declaredPointCount; ++index) {
            if (cancelled(index)) {
                result.cancelled = true;
                result.error = QStringLiteral("已取消");
                return result;
            }
            const qint64 length = file.readLine(buffer.data(), buffer.size());
            if (length <= 0 || (length == buffer.size() - 1 && !file.atEnd()
                                && buffer[length - 1] != '\n')) break;
            pointcloud::Point3D point;
            if (!parseAsciiPoint(QByteArray::fromRawData(buffer.data(), int(length)),
                                 indices, &point)) break;
            result.points[parsedCount++] = point;
            updateBounds(point);
            reportProgress(index + 1);
        }
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
    return result;
}

} // namespace pcv::detail::io
