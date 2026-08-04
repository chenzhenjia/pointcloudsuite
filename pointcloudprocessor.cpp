#include "pointcloudprocessor.h"

#include <QDataStream>
#include <limits>
#include <unordered_map>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QFileInfo>
#include <QHash>
#include <QDataStream>

namespace pointcloud {
namespace {

struct Property { QByteArray type; QByteArray name; };

int scalarBytes(const QByteArray &type) {
    if (type == "char" || type == "int8" || type == "uchar" || type == "uint8") return 1;
    if (type == "short" || type == "int16" || type == "ushort" || type == "uint16") return 2;
    if (type == "int" || type == "int32" || type == "uint" || type == "uint32" || type == "float" || type == "float32") return 4;
    if (type == "double" || type == "float64") return 8;
    return 0;
}

bool parseAsciiPoint(const QByteArray &line, const int indices[6], Point3D &point) {
    const int last = std::max({indices[0], indices[1], indices[2],
                               indices[3], indices[4], indices[5]});
    const char *cursor = line.constData();
    const char *end = cursor + line.size();
    double values[6] = {};
    for (int column = 0; column <= last; ++column) {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')) ++cursor;
        if (cursor >= end) return false;
        char *next = nullptr;
        const double value = std::strtod(cursor, &next);
        if (next == cursor) return false;
        for (int component = 0; component < 6; ++component)
            if (column == indices[component]) values[component] = value;
        cursor = next;
    }
    point = {float(values[0]), float(values[1]), float(values[2]),
             float(values[3]), float(values[4]), float(values[5])};
    return true;
}

double readScalar(QDataStream &stream, const QByteArray &type) {
    if (type == "float" || type == "float32") {
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        float value = 0;
        stream >> value;
        return value;
    }
    if (type == "double" || type == "float64") {
        stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
        double value = 0;
        stream >> value;
        return value;
    }
    if (type == "uchar" || type == "uint8") { quint8 value = 0; stream >> value; return value; }
    if (type == "char" || type == "int8") { qint8 value = 0; stream >> value; return value; }
    if (type == "ushort" || type == "uint16") { quint16 value = 0; stream >> value; return value; }
    if (type == "short" || type == "int16") { qint16 value = 0; stream >> value; return value; }
    if (type == "uint" || type == "uint32") { quint32 value = 0; stream >> value; return value; }
    qint32 value = 0; stream >> value; return value;
}

} // namespace

bool loadPly(const QString &fileName, QVector<Point3D> &points, QString *error) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }

    QByteArray line;
    QString format = QStringLiteral("ascii");
    int vertexCount = 0;
    bool inVertex = false;
    bool headerEnded = false;
    bool unsupportedVertexList = false;
    QVector<Property> properties;
    while (!(line = file.readLine()).isEmpty()) {
        const QByteArray text = line.trimmed();
        if (text == "end_header") {
            headerEnded = true;
            break;
        }
        const QStringList fields = QString::fromLatin1(text).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (fields.isEmpty()) continue;
        if (fields[0] == "format" && fields.size() > 1) format = fields[1];
        else if (fields[0] == "element") {
            inVertex = fields.size() > 1 && fields[1] == "vertex";
            if (inVertex && fields.size() > 2) vertexCount = fields[2].toInt();
        } else if (inVertex && fields[0] == "property" && fields.size() >= 3) {
            if (fields[1] == "list") {
                unsupportedVertexList = true;
            } else {
                Property property;
                property.type = fields[1].toLatin1();
                property.name = fields[2].toLatin1();
                properties.push_back(property);
            }
        }
    }

    if (!headerEnded) {
        if (error) *error = QStringLiteral("PLY 文件头不完整，缺少 end_header");
        return false;
    }
    if (format != QStringLiteral("ascii")
        && format != QStringLiteral("binary_little_endian")
        && format != QStringLiteral("binary_big_endian")) {
        if (error) *error = QStringLiteral("不支持的 PLY 格式：%1").arg(format);
        return false;
    }
    if (unsupportedVertexList) {
        if (error) *error = QStringLiteral("暂不支持 vertex 元素中的 list 属性");
        return false;
    }
    for (const Property &property : properties) {
        if (scalarBytes(property.type) == 0) {
            if (error) *error = QStringLiteral("不支持的 PLY 属性类型：%1")
                                    .arg(QString::fromLatin1(property.type));
            return false;
        }
    }

    int indices[6] = {-1, -1, -1, -1, -1, -1};
    for (int i = 0; i < properties.size(); ++i) {
        if (properties[i].name == "x") indices[0] = i;
        if (properties[i].name == "y") indices[1] = i;
        if (properties[i].name == "z") indices[2] = i;
        if (properties[i].name == "nx") indices[3] = i;
        if (properties[i].name == "ny") indices[4] = i;
        if (properties[i].name == "nz") indices[5] = i;
    }
    if (vertexCount <= 0 || indices[0] < 0 || indices[1] < 0 || indices[2] < 0) {
        if (error) *error = QStringLiteral("PLY 文件缺少有效的 vertex/x/y/z 定义");
        return false;
    }

    points.clear();
    points.reserve(vertexCount);
    const bool ascii = format == "ascii";
    if (ascii) {
        for (int i = 0; i < vertexCount; ++i) {
            const QByteArray vertexLine = file.readLine();
            if (vertexLine.isEmpty()) break;
            Point3D point;
            if (parseAsciiPoint(vertexLine, indices, point)) points.push_back(point);
            if ((i % 100000) == 0)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
        }
    } else {
        QDataStream stream(&file);
        stream.setByteOrder(format.contains("big_endian") ? QDataStream::BigEndian : QDataStream::LittleEndian);
        for (int i = 0; i < vertexCount; ++i) {
            Point3D point;
            for (int column = 0; column < properties.size(); ++column) {
                const double value = readScalar(stream, properties[column].type);
                if (column == indices[0]) point.x = float(value);
                if (column == indices[1]) point.y = float(value);
                if (column == indices[2]) point.z = float(value);
                if (column == indices[3]) point.nx = float(value);
                if (column == indices[4]) point.ny = float(value);
                if (column == indices[5]) point.nz = float(value);
            }
            if (stream.status() != QDataStream::Ok) break;
            points.push_back(point);
        }
    }
    if (points.size() != vertexCount) {
        const qsizetype loadedCount = points.size();
        points.clear();
        if (error) *error = QStringLiteral("PLY 顶点数据不完整：声明 %1 点，实际读取 %2 点")
                                .arg(vertexCount).arg(loadedCount);
        return false;
    }
    return true;
}

QVector<Point3D> proportionalDownsample(const QVector<Point3D> &points, int denominator) {
    denominator = std::max(1, denominator);
    if (denominator == 1 || points.isEmpty()) return points;
    QVector<Point3D> result;
    result.reserve((points.size() + denominator - 1) / denominator);
    for (qsizetype i = 0; i < points.size(); i += denominator) result.push_back(points[int(i)]);
    return result;
}

namespace {
constexpr quint32 CacheMagic = 0x31564350; // PCV1
constexpr quint32 CacheVersion = 1;

QString cachePathFor(const QString &fileName) {
    return fileName + QStringLiteral(".pcvbin");
}

bool readCache(const QString &source, QVector<Point3D> &points) {
    QFileInfo sourceInfo(source);
    QFile cache(cachePathFor(source));
    if (!cache.open(QIODevice::ReadOnly)) return false;
    const QFileInfo cacheInfo(cachePathFor(source));
    if (cache.size() < 32 || cacheInfo.lastModified() < sourceInfo.lastModified()) return false;
    QDataStream stream(&cache);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0;
    quint64 count = 0, sourceSize = 0;
    qint64 sourceStamp = 0;
    stream >> magic >> version >> count >> sourceSize >> sourceStamp;
    if (stream.status() != QDataStream::Ok || magic != CacheMagic || version != CacheVersion
        || sourceSize != quint64(sourceInfo.size()) || sourceStamp != sourceInfo.lastModified().toMSecsSinceEpoch()
        || count > quint64(std::numeric_limits<qsizetype>::max())) return false;
    points.resize(qsizetype(count));
    const qint64 payloadBytes = qint64(count) * qint64(sizeof(Point3D));
    if (cache.read(reinterpret_cast<char *>(points.data()), payloadBytes) != payloadBytes) {
        points.clear();
        return false;
    }
    return true;
}

bool writeCache(const QString &source, const QVector<Point3D> &points) {
    const QFileInfo sourceInfo(source);
    QFile cache(cachePathFor(source));
    if (!cache.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot create point cloud cache:" << cache.errorString();
        return false;
    }
    QDataStream stream(&cache);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << CacheMagic << CacheVersion << quint64(points.size())
           << quint64(sourceInfo.size()) << sourceInfo.lastModified().toMSecsSinceEpoch();
    const qint64 payloadBytes = qint64(points.size()) * qint64(sizeof(Point3D));
    if (cache.write(reinterpret_cast<const char *>(points.constData()), payloadBytes) != payloadBytes) {
        qWarning() << "Cannot write point cloud cache payload:" << cache.errorString();
        return false;
    }
    if (stream.status() != QDataStream::Ok) {
        qWarning() << "Cannot write point cloud cache:" << stream.status();
        return false;
    }
    if (!cache.flush()) {
        qWarning() << "Cannot flush point cloud cache:" << cache.errorString();
        return false;
    }
    cache.close();
    return true;
}
}

bool loadPlyCached(const QString &fileName, QVector<Point3D> &points,
                   QString *error, bool *usedCache) {
    if (usedCache) *usedCache = false;
    QVector<Point3D> cached;
    if (readCache(fileName, cached)) {
        points.swap(cached);
        if (usedCache) *usedCache = true;
        return true;
    }
    QVector<Point3D> parsed;
    if (!loadPly(fileName, parsed, error)) return false;
    writeCache(fileName, parsed);
    points.swap(parsed);
    return true;
}

LoadResult loadPlyCachedResult(const QString &fileName) {
    LoadResult result;
    result.ok = loadPlyCached(fileName, result.points, &result.error, &result.usedCache);
    return result;
}

QVector<Point3D> octreeLod(const QVector<Point3D> &points, qsizetype targetCount) {
    if (points.isEmpty() || targetCount <= 0 || points.size() <= targetCount) return points;
    float minX = points.first().x, maxX = minX, minY = points.first().y, maxY = minY;
    float minZ = points.first().z, maxZ = minZ;
    for (const Point3D &point : points) {
        minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
        minZ = std::min(minZ, point.z); maxZ = std::max(maxZ, point.z);
    }
    const float span = std::max({maxX - minX, maxY - minY, maxZ - minZ, 1.0e-6f});
    int depth = 0;
    while (depth < 12 && (qsizetype(1) << (3 * (depth + 1))) < targetCount * 2) ++depth;
    const int grid = 1 << depth;
    struct Key { int x, y, z; bool operator==(const Key &other) const { return x == other.x && y == other.y && z == other.z; } };
    struct KeyHash { size_t operator()(const Key &key) const { return (size_t(key.x) * 73856093u) ^ (size_t(key.y) * 19349663u) ^ (size_t(key.z) * 83492791u); } };
    std::unordered_map<Key, Point3D, KeyHash> leaves;
    leaves.reserve(size_t(targetCount));
    for (const Point3D &point : points) {
        Key key{qBound(0, int((point.x - minX) / span * grid), grid - 1),
                qBound(0, int((point.y - minY) / span * grid), grid - 1),
                qBound(0, int((point.z - minZ) / span * grid), grid - 1)};
        leaves.emplace(key, point);
    }
    QVector<Point3D> result;
    result.reserve(qMin<qsizetype>(targetCount, qsizetype(leaves.size())));
    for (const auto &entry : leaves) {
        result.push_back(entry.second);
        if (result.size() >= targetCount) break;
    }
    return result;
}

} // namespace pointcloud
