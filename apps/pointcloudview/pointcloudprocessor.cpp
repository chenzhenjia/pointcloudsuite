#include "pointcloudprocessor.h"
#include <pcv/io/cloud_cache.h>
#include <pcv/filtering/downsample.h>
#include <pcv/filtering/statistical_filter.h>
#include <pcv/io/ply_reader.h>
#include <pcv/infrastructure/runtime_paths.h>

#include <array>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QCoreApplication>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdlib>
#include <QFileInfo>
#include <QDataStream>
#include <QtEndian>
#include <QLocale>
#include <QVector2D>
#include <QVector3D>
#include <QQuaternion>
#include <QtMath>
#include <random>
#include <thread>
#include <future>
#include <cstring>

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
    float values[6] = {};
    for (int column = 0; column <= last; ++column) {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')) ++cursor;
        if (cursor >= end) return false;
        char *next = nullptr;
        const float value = std::strtof(cursor, &next);
        if (next == cursor) return false;
        for (int component = 0; component < 6; ++component)
            if (column == indices[component]) values[component] = value;
        cursor = next;
    }
    point = {values[0], values[1], values[2], values[3], values[4], values[5]};
    return true;
}

bool usablePoint(const Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)
        && (std::abs(point.x) + std::abs(point.y) + std::abs(point.z) > 1.0e-8f);
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

double readBinaryScalar(const char *&cursor, const char *end,
                        const QByteArray &type, bool bigEndian, bool *ok) {
    const int bytes = scalarBytes(type);
    if (bytes <= 0 || cursor + bytes > end) { if (ok) *ok = false; return 0.0; }
    const uchar *data = reinterpret_cast<const uchar *>(cursor);
    cursor += bytes;
    if (type == "float" || type == "float32") {
        const quint32 raw = bigEndian ? qFromBigEndian<quint32>(data) : qFromLittleEndian<quint32>(data);
        float value; std::memcpy(&value, &raw, sizeof(value)); return value;
    }
    if (type == "double" || type == "float64") {
        const quint64 raw = bigEndian ? qFromBigEndian<quint64>(data) : qFromLittleEndian<quint64>(data);
        double value; std::memcpy(&value, &raw, sizeof(value)); return value;
    }
    if (type == "uchar" || type == "uint8") return data[0];
    if (type == "char" || type == "int8") return qint8(data[0]);
    if (type == "ushort" || type == "uint16") {
        return bigEndian ? qFromBigEndian<quint16>(data) : qFromLittleEndian<quint16>(data);
    }
    if (type == "short" || type == "int16") {
        const quint16 raw = bigEndian ? qFromBigEndian<quint16>(data) : qFromLittleEndian<quint16>(data);
        return qint16(raw);
    }
    const quint32 raw = bigEndian ? qFromBigEndian<quint32>(data) : qFromLittleEndian<quint32>(data);
    return type == "uint" || type == "uint32" ? raw : qint32(raw);
}

} // namespace

bool loadPlyLegacy(const QString &fileName, QVector<Point3D> &points, QString *error,
                   const LoadProgressCallback &progress) {
    // 先解析 PLY 头部，记录顶点数量、存储格式以及 xyz/法向量所在列。
    // 这样无论属性顺序如何变化，都能按名称读取，而不是依赖固定列号。
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

    // 头部校验通过后再分配输出，避免解析失败时覆盖调用方已有数据。
    points.clear();
    points.reserve(vertexCount);
    const bool ascii = format == "ascii";
    if (ascii) {
        std::array<char, 64 * 1024> lineBuffer{};
        for (int i = 0; i < vertexCount; ++i) {
            const qint64 lineLength = file.readLine(lineBuffer.data(), lineBuffer.size());
            if (lineLength <= 0) break;
            if (lineLength == lineBuffer.size() - 1
                && !file.atEnd()
                && lineBuffer[lineLength - 1] != '\n') {
                if (error) *error = QStringLiteral("第 %1 个顶点行超过 64 KiB").arg(i + 1);
                return false;
            }
            const QByteArray vertexLine = QByteArray::fromRawData(lineBuffer.data(), int(lineLength));
            Point3D point;
            if (parseAsciiPoint(vertexLine, indices, point)) points.push_back(point);
            if (progress && ((i % 10000) == 0 || i + 1 == vertexCount))
                progress(i + 1, vertexCount);
        }
    } else {
        const bool bigEndian = format.contains("big_endian");
        const qsizetype bytesPerVertex = std::accumulate(properties.cbegin(), properties.cend(), qsizetype(0),
            [](qsizetype total, const Property &property) { return total + scalarBytes(property.type); });
        if (bytesPerVertex <= 0 || vertexCount > std::numeric_limits<qsizetype>::max() / bytesPerVertex) {
            if (error) *error = QStringLiteral("PLY 二进制顶点数据大小溢出");
            return false;
        }
        const qsizetype payloadBytes = bytesPerVertex * qsizetype(vertexCount);
        QByteArray payload = file.read(payloadBytes);
        const char *cursor = payload.constData();
        const char *end = cursor + payload.size();
        bool binaryOk = payload.size() == payloadBytes;
        for (int i = 0; i < vertexCount && binaryOk; ++i) {
            Point3D point;
            for (int column = 0; column < properties.size(); ++column) {
                const double value = readBinaryScalar(cursor, end, properties[column].type, bigEndian, &binaryOk);
                if (column == indices[0]) point.x = float(value);
                if (column == indices[1]) point.y = float(value);
                if (column == indices[2]) point.z = float(value);
                if (column == indices[3]) point.nx = float(value);
                if (column == indices[4]) point.ny = float(value);
                if (column == indices[5]) point.nz = float(value);
            }
            points.push_back(point);
            if (progress && ((i % 10000) == 0 || i + 1 == vertexCount))
                progress(i + 1, vertexCount);
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

bool loadPly(const QString &fileName, QVector<Point3D> &points, QString *error,
             const LoadProgressCallback &progress) {
    pcv::detail::io::PlyReadOptions options;
    options.progress = progress;
    pcv::detail::io::PlyReadResult result = pcv::detail::io::readPly(fileName, options);
    if (!result.ok) {
        if (error) *error = result.error;
        return false;
    }
    points.swap(result.points);
    return true;
}

QVector<Point3D> proportionalDownsample(const QVector<Point3D> &points, int denominator) {
    return pcv::detail::filtering::proportionalDownsample(points, denominator);
}

namespace {
constexpr quint32 CacheMagic = 0x31564350; // PCV1
constexpr quint32 CacheVersion = 1;

QString intermediateDirectory() {
    return pcv::runtime::cacheDirectory();
}

QString sourceCacheKey(const QString &fileName) {
    const QByteArray absolutePath = QFileInfo(fileName).absoluteFilePath().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(absolutePath, QCryptographicHash::Sha256).toHex().left(16));
}

QString cachePathFor(const QString &fileName) {
    QString baseName = QFileInfo(fileName).completeBaseName();
    baseName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return QDir(intermediateDirectory()).filePath(
        QStringLiteral("ply_%1_%2.pcvbin").arg(baseName, sourceCacheKey(fileName)));
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
    if (count > quint64(std::numeric_limits<qint64>::max() / qint64(sizeof(Point3D)))) return false;
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
    if (points.size() > qsizetype(std::numeric_limits<qint64>::max() / qint64(sizeof(Point3D)))) return false;
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

bool loadPlyCachedLegacy(const QString &fileName, QVector<Point3D> &points,
                         QString *error, bool *usedCache) {
    // 缓存文件带有源文件大小和修改时间校验；源文件变化时自动回退到完整解析。
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

bool loadPlyCached(const QString &fileName, QVector<Point3D> &points,
                   QString *error, bool *usedCache) {
    pcv::detail::io::CachedCloudResult result = pcv::detail::io::readPlyCached(fileName);
    if (usedCache) *usedCache = result.usedCache;
    if (!result.ok) {
        if (error) *error = result.error;
        return false;
    }
    points.swap(result.points);
    return true;
}

LoadResult loadPlyCachedResult(const QString &fileName) {
    LoadResult result;
    result.ok = loadPlyCached(fileName, result.points, &result.error, &result.usedCache);
    return result;
}

QVector<Point3D> octreeLod(const QVector<Point3D> &points, qsizetype targetCount) {
    // 将包围盒划分为八叉树网格，每个叶节点只保留一个代表点，
    // 在控制点数的同时尽量保持空间分布，适合显示层级细节（LOD）。
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

namespace {
struct NoiseKey {
    qint64 x = 0, y = 0, z = 0;
    bool operator==(const NoiseKey &other) const { return x == other.x && y == other.y && z == other.z; }
};
struct NoiseKeyHash {
    size_t operator()(const NoiseKey &key) const {
        return size_t(key.x * 73856093LL) ^ size_t(key.y * 19349663LL) ^ size_t(key.z * 83492791LL);
    }
};

struct LayerKey {
    qint64 layer = 0;
    qint64 x = 0;
    qint64 y = 0;
    bool operator==(const LayerKey &other) const { return layer == other.layer && x == other.x && y == other.y; }
};
struct LayerKeyHash {
    size_t operator()(const LayerKey &key) const {
        return size_t(key.layer * 73856093LL) ^ size_t(key.x * 19349663LL) ^ size_t(key.y * 83492791LL);
    }
};

NoiseKey noiseKey(const Point3D &point, float cell) {
    return {qFloor(double(point.x) / cell), qFloor(double(point.y) / cell), qFloor(double(point.z) / cell)};
}

LayerKey layerKey(const Point3D &point, float cell, float layerSize) {
    return {qRound64(double(point.z) / layerSize), qFloor(double(point.x) / cell), qFloor(double(point.y) / cell)};
}

QVector<Point3D> voxelFilter(const QVector<Point3D> &points, float voxelSize) {
    return pcv::detail::filtering::voxelDownsample(
        points, voxelSize, pcv::detail::filtering::VoxelRepresentative::FirstInputPoint).points;
}

struct VoxelRepresentatives {
    QVector<Point3D> points;
    QVector<NoiseKey> keys;
};

VoxelRepresentatives voxelRepresentatives(const QVector<Point3D> &points, float voxelSize) {
    VoxelRepresentatives result;
    if (points.isEmpty() || voxelSize <= 0.0f) { result.points = points; return result; }
    std::unordered_map<NoiseKey, int, NoiseKeyHash> indices;
    indices.reserve(size_t(points.size() / 2 + 1));
    for (const Point3D &point : points) {
        if (!usablePoint(point)) continue;
        const NoiseKey key = noiseKey(point, voxelSize);
        if (indices.find(key) != indices.end()) continue;
        indices.emplace(key, result.points.size());
        result.points.push_back(point);
        result.keys.push_back(key);
    }
    return result;
}

constexpr quint32 MergeCacheMagic = 0x314D4350; // PCM1
constexpr quint32 MergeCacheVersion = 8;
QString progressSourceName(WorldCloudInput::ScanProgressSource source);
QString mergeCachePath(const QVector<WorldCloudInput> &inputs) {
    if (inputs.isEmpty()) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const WorldCloudInput &input : inputs) {
        hash.addData(QFileInfo(input.filePath).absoluteFilePath().toUtf8());
        hash.addData("\0", 1);
    }
    return QDir(intermediateDirectory()).filePath(
        QStringLiteral("merge_%1.pcvbin")
            .arg(QString::fromLatin1(hash.result().toHex().left(16))));
}
bool sameTransform(const QMatrix4x4 &a, const QMatrix4x4 &b) {
    const float *x = a.constData(), *y = b.constData();
    for (int i = 0; i < 16; ++i) if (std::abs(x[i] - y[i]) > 1.0e-6f) return false;
    return true;
}

bool validateRigidTransform(const QMatrix4x4 &matrix, QString *error) {
    const float *values = matrix.constData();
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(values[i])) {
            if (error) *error = QStringLiteral("矩阵包含 NaN/Inf");
            return false;
        }
    }
    if (std::abs(matrix(3, 0)) > 1.0e-4f || std::abs(matrix(3, 1)) > 1.0e-4f
        || std::abs(matrix(3, 2)) > 1.0e-4f || std::abs(matrix(3, 3) - 1.0f) > 1.0e-4f) {
        if (error) *error = QStringLiteral("齐次矩阵底行不是 [0 0 0 1]");
        return false;
    }
    const QVector3D c0(matrix(0, 0), matrix(1, 0), matrix(2, 0));
    const QVector3D c1(matrix(0, 1), matrix(1, 1), matrix(2, 1));
    const QVector3D c2(matrix(0, 2), matrix(1, 2), matrix(2, 2));
    const float tolerance = 2.0e-3f;
    if (std::abs(c0.lengthSquared() - 1.0f) > tolerance
        || std::abs(c1.lengthSquared() - 1.0f) > tolerance
        || std::abs(c2.lengthSquared() - 1.0f) > tolerance
        || std::abs(QVector3D::dotProduct(c0, c1)) > tolerance
        || std::abs(QVector3D::dotProduct(c0, c2)) > tolerance
        || std::abs(QVector3D::dotProduct(c1, c2)) > tolerance
        || matrix.determinant() < 0.98f || matrix.determinant() > 1.02f) {
        if (error) *error = QStringLiteral("旋转部分不是有效的刚体旋转矩阵");
        return false;
    }
    return true;
}

bool readMergeCache(const QVector<WorldCloudInput> &inputs, WorldCloudMergeResult &result) {
    for (const auto &input : inputs)
        if (input.scanDirection == WorldCloudInput::ScanDirection::Auto) return false;
    QFile file(mergeCachePath(inputs));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0; quint32 fileCount = 0; quint64 pointCount = 0;
    quint64 pointsBeforeFusion = 0, duplicateCount = 0; float fusionVoxelSize = 0.0f;
    stream >> magic >> version >> fileCount >> pointCount
           >> pointsBeforeFusion >> duplicateCount >> fusionVoxelSize;
    if (magic != MergeCacheMagic || version != MergeCacheVersion || fileCount != quint32(inputs.size())) return false;
    for (const auto &input : inputs) {
        QString path; quint64 size = 0; qint64 stamp = 0; stream >> path >> size >> stamp;
        QFileInfo info(input.filePath);
        if (path != input.filePath || size != quint64(info.size()) || stamp != info.lastModified().toMSecsSinceEpoch()) return false;
        for (int i = 0; i < 16; ++i) { float value = 0; stream >> value; if (std::abs(value - input.startBaseFromFlange.constData()[i]) > 1.0e-6f) return false; }
        for (int i = 0; i < 16; ++i) { float value = 0; stream >> value; if (std::abs(value - input.endBaseFromFlange.constData()[i]) > 1.0e-6f) return false; }
        for (int i = 0; i < 16; ++i) { float value = 0; stream >> value; if (std::abs(value - input.flangeFromDepth.constData()[i]) > 1.0e-6f) return false; }
        quint8 progress = 0; quint8 direction = 0; quint8 coordinateMode = 0;
        quint8 voxel = 0; quint8 robot = 0; float voxelSize = 0;
        stream >> progress >> direction >> coordinateMode >> voxel >> robot >> voxelSize;
        if (progress != quint8(input.scanProgressSource)
            || direction != quint8(input.scanDirection)
            || coordinateMode != quint8(input.scanCoordinateMode)
            || voxel != quint8(input.voxelDownsample)
            || robot != quint8(input.applyRobotTransform)
            || std::abs(voxelSize - input.voxelSize) > 1.0e-6f) return false;
    }
    if (pointCount > quint64(std::numeric_limits<qsizetype>::max())) return false;
    if (pointsBeforeFusion > quint64(std::numeric_limits<qsizetype>::max())
        || duplicateCount > quint64(std::numeric_limits<qsizetype>::max())) return false;
    result.pointsBeforeCrossCloudFusion = qsizetype(pointsBeforeFusion);
    result.crossCloudDuplicateCount = qsizetype(duplicateCount);
    result.crossCloudFusionVoxelSize = fusionVoxelSize;
    result.points.resize(qsizetype(pointCount)); result.cloudIds.resize(qsizetype(pointCount)); result.sourceIndices.resize(qsizetype(pointCount));
    for (qsizetype i = 0; i < result.points.size(); ++i) { qint32 cloud = -1; qint64 source = -1; stream >> result.points[i].x >> result.points[i].y >> result.points[i].z >> result.points[i].nx >> result.points[i].ny >> result.points[i].nz >> cloud >> source; result.cloudIds[i] = cloud; result.sourceIndices[i] = source; }
    result.sourceFiles.clear();
    for (const auto &input : inputs) result.sourceFiles.push_back(input.filePath);
    result.ok = stream.status() == QDataStream::Ok;
    if (result.ok) {
        // A cache hit must not hide the information needed to diagnose robot
        // pose registration.  The cache header has already validated every
        // input transform/progress/voxel setting; summarize the cached world
        // bounds as well so the UI report is equivalent to a fresh merge.
        result.diagnostics = QStringLiteral("合并缓存命中（已校验文件、Start/End、手眼矩阵、扫描进度和体素参数）\n");
        for (int cloudId = 0; cloudId < inputs.size(); ++cloudId) {
            QVector3D minimum(std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max());
            QVector3D maximum(std::numeric_limits<float>::lowest(),
                              std::numeric_limits<float>::lowest(),
                              std::numeric_limits<float>::lowest());
            qsizetype count = 0;
            for (int i = 0; i < result.points.size(); ++i) {
                if (result.cloudIds.value(i, -1) != cloudId || !usablePoint(result.points[i])) continue;
                const Point3D &point = result.points[i];
                minimum.setX(qMin(minimum.x(), point.x));
                minimum.setY(qMin(minimum.y(), point.y));
                minimum.setZ(qMin(minimum.z(), point.z));
                maximum.setX(qMax(maximum.x(), point.x));
                maximum.setY(qMax(maximum.y(), point.y));
                maximum.setZ(qMax(maximum.z(), point.z));
                ++count;
            }
            result.diagnostics += QStringLiteral("%1: robot=%2, progress=%3, world points=%4, bounds=min(%5,%6,%7) max(%8,%9,%10)\n")
                .arg(QFileInfo(inputs[cloudId].filePath).fileName())
                .arg(inputs[cloudId].applyRobotTransform ? QStringLiteral("enabled") : QStringLiteral("disabled"))
                .arg(progressSourceName(inputs[cloudId].scanProgressSource))
                .arg(count)
                .arg(minimum.x(), 0, 'g', 8).arg(minimum.y(), 0, 'g', 8).arg(minimum.z(), 0, 'g', 8)
                .arg(maximum.x(), 0, 'g', 8).arg(maximum.y(), 0, 'g', 8).arg(maximum.z(), 0, 'g', 8);
        }
        result.diagnostics += QStringLiteral(
            "Cross-cloud real-point fusion (cache): before=%1, after=%2, duplicates=%3, voxel=%4 mm\n")
            .arg(result.pointsBeforeCrossCloudFusion).arg(result.points.size())
            .arg(result.crossCloudDuplicateCount).arg(result.crossCloudFusionVoxelSize, 0, 'g', 8);
    }
    return result.ok;
}
void writeMergeCache(const QVector<WorldCloudInput> &inputs, const WorldCloudMergeResult &result) {
    const QString path = mergeCachePath(inputs), temp = path + QStringLiteral(".tmp");
    QFile file(temp); if (!file.open(QIODevice::WriteOnly)) return;
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    stream << MergeCacheMagic << MergeCacheVersion << quint32(inputs.size())
           << quint64(result.points.size())
           << quint64(result.pointsBeforeCrossCloudFusion)
           << quint64(result.crossCloudDuplicateCount)
           << result.crossCloudFusionVoxelSize;
    for (const auto &input : inputs) { QFileInfo info(input.filePath); stream << input.filePath << quint64(info.size()) << info.lastModified().toMSecsSinceEpoch(); for (int i = 0; i < 16; ++i) stream << input.startBaseFromFlange.constData()[i]; for (int i = 0; i < 16; ++i) stream << input.endBaseFromFlange.constData()[i]; for (int i = 0; i < 16; ++i) stream << input.flangeFromDepth.constData()[i]; stream << quint8(input.scanProgressSource) << quint8(input.scanDirection) << quint8(input.scanCoordinateMode) << quint8(input.voxelDownsample) << quint8(input.applyRobotTransform) << input.voxelSize; }
    for (qsizetype i = 0; i < result.points.size(); ++i) { const auto &p = result.points[i]; stream << p.x << p.y << p.z << p.nx << p.ny << p.nz << qint32(result.cloudIds[i]) << qint64(result.sourceIndices[i]); }
    file.flush(); file.close(); QFile::remove(path); QFile::rename(temp, path);
}

struct IcpCorrection { float r[3][3]{{1,0,0},{0,1,0},{0,0,1}}; QVector3D t; bool ok=false; float rms=0; float xyRms=0; float zRms=0; int correspondences=0; int uniqueReferenceCount=0; float angleDegrees=0; };

float twoPointFiveDWeight(const Point3D &point, float edgeWeight, float depthEdgeThreshold) {
    const float normalLength = std::sqrt(point.nx * point.nx + point.ny * point.ny + point.nz * point.nz);
    if (normalLength <= 1.0e-6f || edgeWeight >= 0.999f) return 1.0f;
    const float nz = std::abs(point.nz) / normalLength;
    return nz < qBound(0.05f, depthEdgeThreshold, 0.99f) ? qBound(0.01f, edgeWeight, 1.0f) : 1.0f;
}

QVector3D cloudCentroid(const QVector<Point3D> &points) {
    QVector3D c;
    qsizetype count = 0;
    for (const Point3D &p : points) if (usablePoint(p)) {
        c += QVector3D(p.x, p.y, p.z);
        ++count;
    }
    return count > 0 ? c / float(count) : QVector3D();
}

void translateCloud(QVector<Point3D> &points, const QVector3D &delta) {
    for (Point3D &p : points) {
        p.x += delta.x(); p.y += delta.y(); p.z += delta.z();
    }
}

struct IcpSpatialIndex {
    std::unordered_map<NoiseKey, QVector<int>, NoiseKeyHash> cells;
    float cellSize = 0.0f;
};

IcpSpatialIndex buildIcpSpatialIndex(const QVector<Point3D> &reference, float cellSize) {
    IcpSpatialIndex index;
    index.cellSize = cellSize;
    if (cellSize <= 0.0f) return index;
    index.cells.reserve(size_t(reference.size() / 2 + 1));
    for (int i = 0; i < reference.size(); ++i)
        index.cells[noiseKey(reference[i], cellSize)].push_back(i);
    return index;
}

IcpCorrection estimateIcpCorrection(const QVector<Point3D> &moving,
                                    const QVector<Point3D> &reference,
                                    const IcpSpatialIndex &index,
                                    float maximumDistance, int maximumSamples, int minimumCorrespondences,
                                    float zWeight, float edgeWeight, float depthEdgeThreshold) {
    IcpCorrection out;
    if (moving.size() < 3 || reference.size() < 3 || maximumDistance <= 0 || index.cellSize <= 0.0f) return out;
    struct IcpPair { QVector3D moving; QVector3D reference; int referenceIndex = -1; float distanceSquared = 0.0f; float weight = 1.0f; };
    QVector<IcpPair> pairs;
    const int step = qMax(1, int((moving.size() + maximumSamples - 1) / maximumSamples));
    const float limit2 = maximumDistance * maximumDistance;
    for (int i = 0; i < moving.size(); i += step) {
        const Point3D &p = moving[i]; const NoiseKey key = noiseKey(p, maximumDistance);
        float best = limit2; int bestIndex = -1;
        for (qint64 dx=-1; dx<=1; ++dx) for (qint64 dy=-1; dy<=1; ++dy) for (qint64 dz=-1; dz<=1; ++dz) {
            auto it = index.cells.find({key.x+dx,key.y+dy,key.z+dz}); if (it == index.cells.end()) continue;
            for (int j : it->second) { const Point3D &q=reference[j]; const float ex=p.x-q.x,ey=p.y-q.y,ez=p.z-q.z; const float edge = qMin(twoPointFiveDWeight(p, edgeWeight, depthEdgeThreshold), twoPointFiveDWeight(q, edgeWeight, depthEdgeThreshold)); const float d=edge * (ex*ex+ey*ey+zWeight*ez*ez); if(d<best){best=d;bestIndex=j;} }
        }
        if (bestIndex >= 0) { const Point3D &q = reference[bestIndex]; pairs.push_back({QVector3D(p.x,p.y,p.z), QVector3D(q.x,q.y,q.z), bestIndex, best, qMin(twoPointFiveDWeight(p, edgeWeight, depthEdgeThreshold), twoPointFiveDWeight(q, edgeWeight, depthEdgeThreshold))}); }
    }
    if (pairs.size() < qMax(6, minimumCorrespondences)) return out;
    // Reject the worst residual tail before estimating the rigid correction.
    // This protects planar/repetitive clouds from a small number of wrong
    // nearest-neighbour matches without adding a heavyweight dependency.
    if (pairs.size() >= 40) {
        std::sort(pairs.begin(), pairs.end(), [](const IcpPair &a, const IcpPair &b) {
            return a.distanceSquared < b.distanceSquared;
        });
        const int keep = qMax(qMax(6, minimumCorrespondences), int(std::ceil(pairs.size() * 0.90)));
        if (keep < pairs.size()) pairs.resize(keep);
    }
    out.correspondences = pairs.size();
    {
        std::unordered_set<int> unique;
        unique.reserve(size_t(pairs.size()));
        for (const auto &pair : pairs) unique.insert(pair.referenceIndex);
        out.uniqueReferenceCount = int(unique.size());
    }
    double weightSum = 0.0; QVector3D cp, cq; for (const auto &v:pairs){weightSum += v.weight; cp += v.moving * v.weight; cq += v.reference * v.weight;} if (weightSum <= 1.0e-6) return out; cp/=float(weightSum); cq/=float(weightSum);
    double s[3][3]{}; for(const auto &v:pairs){QVector3D a=v.moving-cp,b=v.reference-cq; s[0][0]+=v.weight*a.x()*b.x();s[0][1]+=v.weight*a.x()*b.y();s[0][2]+=v.weight*a.x()*b.z();s[1][0]+=v.weight*a.y()*b.x();s[1][1]+=v.weight*a.y()*b.y();s[1][2]+=v.weight*a.y()*b.z();s[2][0]+=v.weight*a.z()*b.x();s[2][1]+=v.weight*a.z()*b.y();s[2][2]+=v.weight*a.z()*b.z();}
    const double tr=s[0][0]+s[1][1]+s[2][2]; double n[4][4]={{tr,s[1][2]-s[2][1],s[2][0]-s[0][2],s[0][1]-s[1][0]},{s[1][2]-s[2][1],s[0][0]-s[1][1]-s[2][2],s[0][1]+s[1][0],s[0][2]+s[2][0]},{s[2][0]-s[0][2],s[0][1]+s[1][0],-s[0][0]+s[1][1]-s[2][2],s[1][2]+s[2][1]},{s[0][1]-s[1][0],s[0][2]+s[2][0],s[1][2]+s[2][1],-s[0][0]-s[1][1]+s[2][2]}};
    double q[4]{1,0,0,0}; for(int k=0;k<32;++k){double v[4]{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)v[r]+=n[r][c]*q[c];double len=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]+v[3]*v[3]);if(len<1e-15)return out;for(int r=0;r<4;++r)q[r]=v[r]/len;}
    const double w=q[0],x=q[1],y=q[2],z=q[3]; out.r[0][0]=float(1-2*(y*y+z*z));out.r[0][1]=float(2*(x*y-z*w));out.r[0][2]=float(2*(x*z+y*w));out.r[1][0]=float(2*(x*y+z*w));out.r[1][1]=float(1-2*(x*x+z*z));out.r[1][2]=float(2*(y*z-x*w));out.r[2][0]=float(2*(x*z-y*w));out.r[2][1]=float(2*(y*z+x*w));out.r[2][2]=float(1-2*(x*x+y*y));
    auto rotate=[&](const QVector3D &v){return QVector3D(out.r[0][0]*v.x()+out.r[0][1]*v.y()+out.r[0][2]*v.z(),out.r[1][0]*v.x()+out.r[1][1]*v.y()+out.r[1][2]*v.z(),out.r[2][0]*v.x()+out.r[2][1]*v.y()+out.r[2][2]*v.z());}; out.t=cq-rotate(cp); out.angleDegrees=qRadiansToDegrees(float(2.0*std::acos(qBound(-1.0, std::min(1.0, std::abs(w)), 1.0))));
    double error=0, xyError=0, zError=0; for(const auto &v:pairs){ const QVector3D residual=rotate(v.moving)+out.t-v.reference; error += v.weight * residual.lengthSquared(); xyError += v.weight * (residual.x()*residual.x()+residual.y()*residual.y()); zError += v.weight * residual.z()*residual.z(); }
    out.rms=float(std::sqrt(error/weightSum)); out.xyRms=float(std::sqrt(xyError/weightSum)); out.zRms=float(std::sqrt(zError/weightSum)); out.ok=true;return out;
}

void applyIcpCorrection(QVector<Point3D> &points, const IcpCorrection &c) { for(Point3D &p:points){const float x=p.x,y=p.y,z=p.z;p.x=c.r[0][0]*x+c.r[0][1]*y+c.r[0][2]*z+c.t.x();p.y=c.r[1][0]*x+c.r[1][1]*y+c.r[1][2]*z+c.t.y();p.z=c.r[2][0]*x+c.r[2][1]*y+c.r[2][2]*z+c.t.z();const float nx=p.nx,ny=p.ny,nz=p.nz;p.nx=c.r[0][0]*nx+c.r[0][1]*ny+c.r[0][2]*nz;p.ny=c.r[1][0]*nx+c.r[1][1]*ny+c.r[1][2]*nz;p.nz=c.r[2][0]*nx+c.r[2][1]*ny+c.r[2][2]*nz;} }

QMatrix4x4 interpolateRigidTransform(const QMatrix4x4 &start, const QMatrix4x4 &end, float t) {
    const QVector3D startPosition(start(0,3), start(1,3), start(2,3));
    const QVector3D endPosition(end(0,3), end(1,3), end(2,3));
    QMatrix3x3 startRotation, endRotation;
    for (int row=0; row<3; ++row) for (int column=0; column<3; ++column) {
        startRotation(row,column)=start(row,column); endRotation(row,column)=end(row,column);
    }
    const QQuaternion rotation = QQuaternion::slerp(
        QQuaternion::fromRotationMatrix(startRotation),
        QQuaternion::fromRotationMatrix(endRotation), t).normalized();
    const QVector3D position = startPosition * (1.0f-t) + endPosition * t;
    QMatrix4x4 result; result.setToIdentity(); result.rotate(rotation);
    result(0,3)=position.x(); result(1,3)=position.y(); result(2,3)=position.z();
    return result;
}

struct RigidTransformLut {
    static constexpr int Resolution = 2048;
    QVector<QMatrix4x4> values;
    bool constant = false;

    RigidTransformLut(const QMatrix4x4 &start, const QMatrix4x4 &end) {
        values.reserve(Resolution + 1);
        constant = true;
        for (int i = 0; i < 16; ++i)
            if (std::abs(start.constData()[i] - end.constData()[i]) > 1.0e-7f) { constant = false; break; }
        if (constant) { values.push_back(start); return; }
        for (int i = 0; i <= Resolution; ++i)
            values.push_back(interpolateRigidTransform(start, end, float(i) / float(Resolution)));
    }

    const QMatrix4x4 &at(float t) const {
        if (constant) return values.front();
        const int index = qBound(0, qRound(t * float(Resolution)), Resolution);
        return values[index];
    }
};

float scanProgress(const QVector<Point3D> &points, qsizetype index,
                   WorldCloudInput::ScanProgressSource source, float low, float high,
                   bool reverseAxis) {
    if (points.size() < 2 || source == WorldCloudInput::ScanProgressSource::VertexOrder)
        return points.size() < 2 ? 0.0f : float(index) / float(points.size() - 1);
    const int axis = source == WorldCloudInput::ScanProgressSource::LocalX ? 0
                   : source == WorldCloudInput::ScanProgressSource::LocalY ? 1 : 2;
    const Point3D &p = points[index];
    const float value = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
    if (high <= low) return 0.0f;
    const float progress = qBound(0.0f, (value-low)/(high-low), 1.0f);
    return reverseAxis ? 1.0f - progress : progress;
}

struct CloudBounds {
    QVector3D minimum{std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max()};
    QVector3D maximum{std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest()};
    bool valid = false;
};

float xyOverlapArea(const CloudBounds &a, const CloudBounds &b, float *movingArea,
                    float *unionArea, float *coverage, float *iou) {
    if (!a.valid || !b.valid) { if (movingArea) *movingArea = 0; if (unionArea) *unionArea = 0; if (coverage) *coverage = 0; if (iou) *iou = 0; return 0; }
    const float ix = qMax(0.0f, qMin(a.maximum.x(), b.maximum.x()) - qMax(a.minimum.x(), b.minimum.x()));
    const float iy = qMax(0.0f, qMin(a.maximum.y(), b.maximum.y()) - qMax(a.minimum.y(), b.minimum.y()));
    const float intersection = ix * iy;
    const float areaA = qMax(0.0f, a.maximum.x() - a.minimum.x()) * qMax(0.0f, a.maximum.y() - a.minimum.y());
    const float areaB = qMax(0.0f, b.maximum.x() - b.minimum.x()) * qMax(0.0f, b.maximum.y() - b.minimum.y());
    const float uni = areaA + areaB - intersection;
    if (movingArea) *movingArea = areaA;
    if (unionArea) *unionArea = uni;
    if (coverage) *coverage = areaA > 1.0e-6f ? intersection / areaA : 0.0f;
    if (iou) *iou = uni > 1.0e-6f ? intersection / uni : 0.0f;
    return intersection;
}

CloudBounds cloudBounds(const QVector<Point3D> &points) {
    CloudBounds bounds;
    for (const Point3D &point : points) {
        if (!usablePoint(point)) continue;
        const QVector3D value(point.x, point.y, point.z);
        bounds.minimum.setX(qMin(bounds.minimum.x(), value.x()));
        bounds.minimum.setY(qMin(bounds.minimum.y(), value.y()));
        bounds.minimum.setZ(qMin(bounds.minimum.z(), value.z()));
        bounds.maximum.setX(qMax(bounds.maximum.x(), value.x()));
        bounds.maximum.setY(qMax(bounds.maximum.y(), value.y()));
        bounds.maximum.setZ(qMax(bounds.maximum.z(), value.z()));
        bounds.valid = true;
    }
    return bounds;
}

QString formatMatrix(const QMatrix4x4 &matrix) {
    QStringList rows;
    for (int row = 0; row < 4; ++row) {
        QStringList values;
        for (int column = 0; column < 4; ++column)
            values << QString::number(matrix(row, column), 'g', 8);
        rows << values.join(QLatin1Char(' '));
    }
    return rows.join(QStringLiteral(" | "));
}

QString formatBounds(const CloudBounds &bounds) {
    if (!bounds.valid) return QStringLiteral("invalid");
    return QStringLiteral("min(%1,%2,%3) max(%4,%5,%6)")
        .arg(bounds.minimum.x(), 0, 'g', 8).arg(bounds.minimum.y(), 0, 'g', 8)
        .arg(bounds.minimum.z(), 0, 'g', 8).arg(bounds.maximum.x(), 0, 'g', 8)
        .arg(bounds.maximum.y(), 0, 'g', 8).arg(bounds.maximum.z(), 0, 'g', 8);
}

QString progressSourceName(WorldCloudInput::ScanProgressSource source) {
    switch (source) {
    case WorldCloudInput::ScanProgressSource::LocalX: return QStringLiteral("LocalX");
    case WorldCloudInput::ScanProgressSource::LocalY: return QStringLiteral("LocalY");
    case WorldCloudInput::ScanProgressSource::LocalZ: return QStringLiteral("LocalZ");
    case WorldCloudInput::ScanProgressSource::VertexOrder: break;
    }
    return QStringLiteral("VertexOrder");
}

WorldCloudMergeResult mergePlyCloudsInWorldImpl(const QVector<WorldCloudInput> &inputs, const IcpOptions &icp) {
    WorldCloudMergeResult result;
    if (inputs.isEmpty()) { result.error = QStringLiteral("未选择 PLY 文件"); return result; }
    for (int index = 0; index < inputs.size(); ++index) {
        QString matrixError;
        if (!validateRigidTransform(inputs[index].startBaseFromFlange, &matrixError)) {
            result.error = QStringLiteral("第 %1 个 PLY 的 Start 位姿无效：%2")
                .arg(index + 1).arg(matrixError);
            return result;
        }
        if (!validateRigidTransform(inputs[index].endBaseFromFlange, &matrixError)) {
            result.error = QStringLiteral("第 %1 个 PLY 的 End 位姿无效：%2")
                .arg(index + 1).arg(matrixError);
            return result;
        }
        if (inputs[index].applyRobotTransform
            && !validateRigidTransform(inputs[index].flangeFromDepth, &matrixError)) {
            result.error = QStringLiteral("第 %1 个 PLY 的手眼矩阵无效：%2")
                .arg(index + 1).arg(matrixError);
            return result;
        }
    }
    if (!icp.enabled && readMergeCache(inputs, result)) return result;
    qsizetype total = 0;
    std::vector<std::future<LoadResult>> futures;
    futures.reserve(inputs.size());
    for (const WorldCloudInput &input : inputs) {
        futures.push_back(std::async(std::launch::async, [path = input.filePath]() {
            return loadPlyCachedResult(path);
        }));
    }
    QVector<QVector<Point3D>> loaded(inputs.size());
    for (int i = 0; i < inputs.size(); ++i) {
        LoadResult item = futures[i].get();
        if (!item.ok) { result.error = QStringLiteral("加载 %1 失败：%2").arg(inputs[i].filePath, item.error); return result; }
        if (item.points.isEmpty()) { result.error = QStringLiteral("%1 不包含可显示的顶点").arg(inputs[i].filePath); return result; }
        total += item.points.size(); loaded[i] = std::move(item.points);
    }
    result.points.reserve(total); result.cloudIds.reserve(total); result.sourceIndices.reserve(total);
    result.sourceFiles.reserve(inputs.size());
    result.icpDiagnostics.reserve(inputs.size());
    result.overlapDiagnostics.reserve(inputs.size());
    CloudBounds accumulatedBounds;
    for (int cloudId = 0; cloudId < inputs.size(); ++cloudId) {
        result.sourceFiles.push_back(inputs[cloudId].filePath);
        QVector<Point3D> points = std::move(loaded[cloudId]);
        const CloudBounds localBounds = cloudBounds(points);
        QVector<qsizetype> sourceIndices(points.size());
        const auto progressSource = inputs[cloudId].scanProgressSource;
        const int progressAxis = progressSource == WorldCloudInput::ScanProgressSource::LocalX ? 0
            : progressSource == WorldCloudInput::ScanProgressSource::LocalY ? 1 : 2;
        float progressLow = std::numeric_limits<float>::max(), progressHigh = -progressLow;
        float firstProgressValue = 0.0f, lastProgressValue = 0.0f;
        bool haveFirstProgressValue = false, haveLastProgressValue = false;
        const RigidTransformLut motionLut(inputs[cloudId].startBaseFromFlange,
                                          inputs[cloudId].endBaseFromFlange);
        if (progressSource != WorldCloudInput::ScanProgressSource::VertexOrder)
            for (const Point3D &point : points) {
                const float value = progressAxis == 0 ? point.x : progressAxis == 1 ? point.y : point.z;
                progressLow = qMin(progressLow, value); progressHigh = qMax(progressHigh, value);
            }
        if (progressSource != WorldCloudInput::ScanProgressSource::VertexOrder) {
            for (const Point3D &point : points) {
                if (!usablePoint(point)) continue;
                const float value = progressAxis == 0 ? point.x : progressAxis == 1 ? point.y : point.z;
                if (!haveFirstProgressValue) { firstProgressValue = value; haveFirstProgressValue = true; }
                lastProgressValue = value;
                haveLastProgressValue = true;
            }
        }
        const QVector3D robotDelta(
            inputs[cloudId].endBaseFromFlange(0, 3) - inputs[cloudId].startBaseFromFlange(0, 3),
            inputs[cloudId].endBaseFromFlange(1, 3) - inputs[cloudId].startBaseFromFlange(1, 3),
            inputs[cloudId].endBaseFromFlange(2, 3) - inputs[cloudId].startBaseFromFlange(2, 3));
        const float robotTravel = robotDelta.length();
        const float localScanSpan = progressSource == WorldCloudInput::ScanProgressSource::VertexOrder
            ? 0.0f : qMax(0.0f, progressHigh - progressLow);
        const float embeddedMotionRatio = robotTravel > 1.0e-5f
            ? localScanSpan / robotTravel : 0.0f;
        bool coordinateEmbedsMotion =
            inputs[cloudId].scanCoordinateMode == WorldCloudInput::ScanCoordinateMode::EmbeddedMotion;
        if (inputs[cloudId].scanCoordinateMode == WorldCloudInput::ScanCoordinateMode::Auto) {
            // A line-scan PLY commonly stores the calibrated scan travel in
            // LocalY already. Applying the Start/End translation again would
            // either double that travel or cancel it to an almost flat strip.
            coordinateEmbedsMotion = inputs[cloudId].applyRobotTransform
                && progressSource != WorldCloudInput::ScanProgressSource::VertexOrder
                && robotTravel > 1.0e-5f
                && embeddedMotionRatio >= 0.75f && embeddedMotionRatio <= 1.25f;
        }
        const bool anchorAtStart = !haveFirstProgressValue || !haveLastProgressValue
            || std::abs(firstProgressValue) <= std::abs(lastProgressValue);
        bool reverseProgress = haveFirstProgressValue && haveLastProgressValue
            && lastProgressValue < firstProgressValue;
        if (inputs[cloudId].scanDirection == WorldCloudInput::ScanDirection::Forward)
            reverseProgress = false;
        else if (inputs[cloudId].scanDirection == WorldCloudInput::ScanDirection::Reverse)
            reverseProgress = true;
        QString progressDecision = reverseProgress ? QStringLiteral("axis-decreasing initial reverse")
                                                    : QStringLiteral("axis-forward initial");
        if (coordinateEmbedsMotion) {
            progressDecision = QStringLiteral(
                "PLY scan axis already embeds motion (local span=%1, robot travel=%2, ratio=%3); fixed %4 pose")
                .arg(localScanSpan, 0, 'g', 8).arg(robotTravel, 0, 'g', 8)
                .arg(embeddedMotionRatio, 0, 'g', 6)
                .arg(anchorAtStart ? QStringLiteral("Start") : QStringLiteral("End"));
        }
        // The local axis direction alone is not enough for Eye-in-Hand scans:
        // the hand-eye/flange rotations can already reverse that axis in the
        // world frame. Compare the transformed first/last valid samples for
        // both pairings and select the candidate with the smaller residual.
        if (!coordinateEmbedsMotion
            && inputs[cloudId].scanDirection == WorldCloudInput::ScanDirection::Auto
            && inputs[cloudId].applyRobotTransform && progressSource != WorldCloudInput::ScanProgressSource::VertexOrder
            && haveFirstProgressValue && haveLastProgressValue && progressHigh > progressLow) {
            const qsizetype validCount = std::count_if(points.cbegin(), points.cend(),
                [](const Point3D &point) { return usablePoint(point); });
            const qsizetype window = qMax<qsizetype>(1, qMin<qsizetype>(4096, validCount / 100));
            QVector3D firstMean, lastMean; qsizetype ordinal = 0;
            for (const Point3D &point : points) {
                if (!usablePoint(point)) continue;
                if (ordinal < window) firstMean += QVector3D(point.x, point.y, point.z);
                if (ordinal >= validCount - window) lastMean += QVector3D(point.x, point.y, point.z);
                ++ordinal;
            }
            firstMean /= float(window); lastMean /= float(window);
            const QMatrix4x4 midTransform = interpolateRigidTransform(
                inputs[cloudId].startBaseFromFlange,
                inputs[cloudId].endBaseFromFlange, 0.5f) * inputs[cloudId].flangeFromDepth;
            const QVector3D cameraWorldDelta = midTransform.mapVector(lastMean - firstMean);
            const float forwardResidual = (cameraWorldDelta + robotDelta).length();
            const float reverseResidual = (cameraWorldDelta - robotDelta).length();
            reverseProgress = reverseResidual < forwardResidual;
            progressDecision = QStringLiteral("auto first/last 1%% mean, forward residual=%1, reverse residual=%2, selected=%3")
                .arg(forwardResidual, 0, 'g', 8).arg(reverseResidual, 0, 'g', 8)
                .arg(reverseProgress ? QStringLiteral("reverse") : QStringLiteral("forward"));
        }
        result.diagnostics += QStringLiteral("%1\n  Robot transform: %2\n  Progress source: %3, range: %4 .. %5\n  Start matrix: %6\n  Mid matrix: %7\n  End matrix: %8\n  Hand-eye matrix (flange<-depth): %9\n  Local bounds: %10\n")
            .arg(QFileInfo(inputs[cloudId].filePath).fileName())
            .arg(inputs[cloudId].applyRobotTransform ? QStringLiteral("enabled") : QStringLiteral("disabled"))
            .arg(progressSourceName(progressSource))
            .arg(progressSource == WorldCloudInput::ScanProgressSource::VertexOrder
                     ? QStringLiteral("0") : QString::number(progressLow, 'g', 8))
            .arg(progressSource == WorldCloudInput::ScanProgressSource::VertexOrder
                     ? QStringLiteral("1") : QString::number(progressHigh, 'g', 8))
            .arg(formatMatrix(inputs[cloudId].startBaseFromFlange))
            .arg(formatMatrix(interpolateRigidTransform(inputs[cloudId].startBaseFromFlange,
                                                        inputs[cloudId].endBaseFromFlange, 0.5f)))
            .arg(formatMatrix(inputs[cloudId].endBaseFromFlange))
            .arg(formatMatrix(inputs[cloudId].flangeFromDepth))
            .arg(formatBounds(localBounds));
        if (progressSource != WorldCloudInput::ScanProgressSource::VertexOrder)
            result.diagnostics += QStringLiteral("  Progress first/last: %1 -> %2 (%3)\n")
                .arg(firstProgressValue, 0, 'g', 8).arg(lastProgressValue, 0, 'g', 8)
                .arg(progressDecision);
        for (qsizetype sourceIndex=0; sourceIndex<points.size(); ++sourceIndex) {
            Point3D &source=points[sourceIndex]; sourceIndices[sourceIndex]=sourceIndex;
            if (inputs[cloudId].applyRobotTransform) {
                const float progress = coordinateEmbedsMotion ? 0.0f
                    : scanProgress(points, sourceIndex, progressSource, progressLow, progressHigh,
                                   reverseProgress);
                const QMatrix4x4 &baseFromFlange = coordinateEmbedsMotion
                    ? (anchorAtStart ? inputs[cloudId].startBaseFromFlange
                                     : inputs[cloudId].endBaseFromFlange)
                    : motionLut.at(progress);
                const QMatrix4x4 transform = baseFromFlange * inputs[cloudId].flangeFromDepth;
                const QVector3D world = transform.map(QVector3D(source.x, source.y, source.z));
                const QVector3D normal = transform.mapVector(QVector3D(source.nx, source.ny, source.nz));
                source.x=world.x();source.y=world.y();source.z=world.z();
                source.nx=normal.x();source.ny=normal.y();source.nz=normal.z();
            }
        }
        if (inputs[cloudId].voxelDownsample && inputs[cloudId].voxelSize > 0.0f) {
            std::unordered_map<NoiseKey, qsizetype, NoiseKeyHash> representatives;
            QVector<Point3D> reduced; QVector<qsizetype> reducedIndices;
            representatives.reserve(size_t(points.size()/2+1));
            for (qsizetype i=0;i<points.size();++i) if (usablePoint(points[i]) && representatives.emplace(noiseKey(points[i],inputs[cloudId].voxelSize),i).second) { reduced.push_back(points[i]); reducedIndices.push_back(sourceIndices[i]); }
            points=std::move(reduced); sourceIndices=std::move(reducedIndices);
        }
        result.diagnostics += QStringLiteral("  World bounds: %1\n  Output points after voxel: %2\n")
            .arg(formatBounds(cloudBounds(points))).arg(points.size());
        const CloudBounds worldBounds = cloudBounds(points);
        float worldOverlapForIcp = 1.0f;
        bool lowWorldOverlapForIcp = false;
        if (cloudId > 0 && accumulatedBounds.valid && worldBounds.valid) {
            WorldCloudMergeResult::OverlapDiagnostic overlap;
            overlap.cloudId = cloudId;
            overlap.intersectionArea = xyOverlapArea(worldBounds, accumulatedBounds,
                                                     &overlap.movingArea, &overlap.unionArea,
                                                     &overlap.movingCoverage,
                                                     &overlap.intersectionOverUnion);
            overlap.warning = overlap.movingCoverage < 0.10f;
            result.overlapDiagnostics.push_back(overlap);
            worldOverlapForIcp = overlap.movingCoverage;
            lowWorldOverlapForIcp = icp.rejectIcpWhenLowWorldOverlap
                && worldOverlapForIcp < qBound(0.0f, icp.minimumWorldOverlapForIcp, 1.0f);
            result.diagnostics += QStringLiteral("  XY overlap with accumulated: intersection=%1, moving coverage=%2, IoU=%3%4\n")
                .arg(overlap.intersectionArea, 0, 'g', 8)
                .arg(overlap.movingCoverage, 0, 'g', 6)
                .arg(overlap.intersectionOverUnion, 0, 'g', 6)
                .arg(overlap.warning ? QStringLiteral(" [WARNING: low overlap]") : QString());
        }
        QVector<Point3D> pointsBeforeIcp;
        if (icp.enabled && cloudId > 0) pointsBeforeIcp = points;
        IcpDiagnostics diag;
        diag.filePath = inputs[cloudId].filePath;
        if (icp.enabled && cloudId > 0 && !result.points.isEmpty() && lowWorldOverlapForIcp) {
            diag.attempted = false;
            diag.reason = QStringLiteral("机器人世界坐标 XY 重合率过低，已跳过 ICP（请检查 Start/End、扫描方向、手眼矩阵和文件顺序）");
            result.diagnostics += QStringLiteral("%1: %2\n")
                .arg(QFileInfo(inputs[cloudId].filePath).fileName(), diag.reason);
        } else if (icp.enabled && cloudId > 0 && !result.points.isEmpty()) {
            diag.attempted = true;
            const int sampleLimit = qMax(1, icp.maximumSamples);
            const int sampleStep = qMax(1, int((points.size() + sampleLimit - 1) / sampleLimit));
            diag.movingSampleCount = (points.size() + sampleStep - 1) / sampleStep;
            if (!inputs[cloudId].applyRobotTransform && icp.globalInitialization) {
                const QVector3D delta = cloudCentroid(result.points) - cloudCentroid(points);
                translateCloud(points, delta);
                diag.usedGlobalInitialization = true;
            }
            IcpCorrection last;
            QVector<float> levels = icp.useMultiScale && !icp.voxelLevels.isEmpty()
                ? icp.voxelLevels : QVector<float>{0.0f};
            int levelNumber = 0;
            for (float levelSize : levels) {
                QVector<Point3D> movingLevel = levelSize > 0.0f ? voxelFilter(points, levelSize) : points;
                QVector<Point3D> referenceLevel = levelSize > 0.0f ? voxelFilter(result.points, levelSize) : result.points;
                if (movingLevel.size() < qMax(6, icp.minimumCorrespondences)
                    || referenceLevel.size() < qMax(6, icp.minimumCorrespondences)) continue;
                const float levelDistance = levelSize > 0.0f
                    ? qMax(icp.maximumCorrespondenceDistance, levelSize * 1.5f)
                    : icp.maximumCorrespondenceDistance;
                const IcpSpatialIndex referenceIndex = buildIcpSpatialIndex(referenceLevel, levelDistance);
                float previous=std::numeric_limits<float>::max();
                const int levelIterations = qMax(1, icp.maximumIterations / qMax(1, int(levels.size())));
                for(int iteration=0;iteration<levelIterations;++iteration){
                    IcpCorrection correction=estimateIcpCorrection(movingLevel,referenceLevel,referenceIndex,levelDistance,icp.maximumSamples,icp.minimumCorrespondences,
                                                                     icp.useTwoPointFiveD ? qBound(0.01f, icp.zWeight, 1.0f) : 1.0f,
                                                                     icp.useTwoPointFiveD ? qBound(0.01f, icp.edgeWeight, 1.0f) : 1.0f,
                                                                     icp.depthEdgeThreshold);
                    if(!correction.ok){ diag.reason = QStringLiteral("有效对应点不足"); break; }
                    if(correction.t.length()>icp.maximumCorrectionTranslation || correction.angleDegrees>icp.maximumCorrectionAngleDegrees){ diag.reason = QStringLiteral("ICP 修正量超过限制"); break; }
                    applyIcpCorrection(points,correction); applyIcpCorrection(movingLevel,correction); last=correction; diag.iterations++;
                    if(std::abs(previous-correction.rms)<=icp.convergenceTolerance){ diag.converged=true; break; }
                    previous=correction.rms;
                }
                if (last.ok) ++levelNumber;
                if (!diag.reason.isEmpty() && !diag.converged) break;
            }
            if (last.ok) {
                if (!diag.converged && diag.iterations > 0 && diag.reason.isEmpty())
                    diag.converged = true;
                diag.correspondences = last.correspondences;
                diag.fitness = diag.movingSampleCount > 0 ? float(last.correspondences) / float(diag.movingSampleCount) : 0.0f;
                diag.overlapRatio = diag.fitness;
                diag.uniqueReferenceRatio = last.correspondences > 0
                    ? float(last.uniqueReferenceCount) / float(last.correspondences) : 0.0f;
                diag.duplicateCorrespondenceRatio = 1.0f - diag.uniqueReferenceRatio;
                diag.rmse = last.rms;
                diag.xyRmse = last.xyRms;
                diag.zRmse = last.zRms;
                diag.completedLevels = levelNumber;
                diag.correctionTranslation = last.t.length();
                diag.correctionAngleDegrees = last.angleDegrees;
                diag.accepted = diag.converged && diag.fitness >= icp.fitnessThreshold && diag.rmse <= icp.rmseThreshold
                    && diag.overlapRatio >= icp.minimumOverlapRatio
                    && diag.uniqueReferenceRatio >= icp.minimumUniqueReferenceRatio;
                if (diag.reason.isEmpty()) {
                    if (diag.overlapRatio < icp.minimumOverlapRatio)
                        diag.reason = QStringLiteral("重合率过低");
                    else if (diag.uniqueReferenceRatio < icp.minimumUniqueReferenceRatio)
                        diag.reason = QStringLiteral("重复对应过高");
                    else
                        diag.reason = diag.accepted ? QStringLiteral("通过 Fitness/RMSE/重合率") : QStringLiteral("Fitness 或 RMSE 未达标");
                }
            }
        } else if (cloudId == 0) {
            diag.filePath = inputs[cloudId].filePath;
            diag.reason = QStringLiteral("参考点云");
        } else if (!icp.enabled) {
            diag.reason = QStringLiteral("ICP 未启用");
        }
        if (diag.attempted && !diag.accepted) {
            // ICP operates on a candidate copy. A failed quality gate must
            // never overwrite the robot-world initial result.
            points = pointsBeforeIcp;
            if (!diag.reason.contains(QStringLiteral("已回滚")))
                diag.reason += QStringLiteral("；已回滚到机器人位姿初值");
        }
        const CloudBounds finalizedBounds = cloudBounds(points);
        if (finalizedBounds.valid) {
            if (!accumulatedBounds.valid) accumulatedBounds = finalizedBounds;
            else {
                accumulatedBounds.minimum.setX(qMin(accumulatedBounds.minimum.x(), finalizedBounds.minimum.x()));
                accumulatedBounds.minimum.setY(qMin(accumulatedBounds.minimum.y(), finalizedBounds.minimum.y()));
                accumulatedBounds.minimum.setZ(qMin(accumulatedBounds.minimum.z(), finalizedBounds.minimum.z()));
                accumulatedBounds.maximum.setX(qMax(accumulatedBounds.maximum.x(), finalizedBounds.maximum.x()));
                accumulatedBounds.maximum.setY(qMax(accumulatedBounds.maximum.y(), finalizedBounds.maximum.y()));
                accumulatedBounds.maximum.setZ(qMax(accumulatedBounds.maximum.z(), finalizedBounds.maximum.z()));
            }
        }
        result.icpDiagnostics.push_back(diag);
        if (icp.enabled && cloudId > 0 && diag.attempted && !diag.accepted && diag.reason.isEmpty())
            result.diagnostics += QStringLiteral("%1: 纯视觉/ICP 指标未达标，可检查点云畸变或机器人位姿；\n").arg(QFileInfo(inputs[cloudId].filePath).fileName());
        for (qsizetype sourceIndex = 0; sourceIndex < points.size(); ++sourceIndex) {
            const Point3D &point = points[sourceIndex];
            result.points.push_back(point); result.cloudIds.push_back(cloudId); result.sourceIndices.push_back(sourceIndices[sourceIndex]);
        }
    }

    result.pointsBeforeCrossCloudFusion = result.points.size();
    float fusionVoxelSize = std::numeric_limits<float>::max();
    for (const WorldCloudInput &input : inputs) {
        if (input.voxelDownsample && input.voxelSize > 0.0f)
            fusionVoxelSize = qMin(fusionVoxelSize, input.voxelSize);
    }
    if (!std::isfinite(fusionVoxelSize) || fusionVoxelSize <= 0.0f)
        fusionVoxelSize = 0.25f;
    result.crossCloudFusionVoxelSize = fusionVoxelSize;

    if (inputs.size() > 1 && !result.points.isEmpty()) {
        std::unordered_map<NoiseKey, qsizetype, NoiseKeyHash> representatives;
        representatives.reserve(size_t(result.points.size() / 2 + 1));
        QVector<Point3D> fusedPoints;
        QVector<int> fusedCloudIds;
        QVector<qsizetype> fusedSourceIndices;
        fusedPoints.reserve(result.points.size());
        fusedCloudIds.reserve(result.cloudIds.size());
        fusedSourceIndices.reserve(result.sourceIndices.size());

        for (qsizetype i = 0; i < result.points.size(); ++i) {
            const Point3D &point = result.points[i];
            if (usablePoint(point)) {
                const NoiseKey key = noiseKey(point, fusionVoxelSize);
                const auto existing = representatives.find(key);
                if (existing != representatives.end()
                    && fusedCloudIds[existing->second] != result.cloudIds[i]) {
                    // Cross-frame duplicate: retain the first real measured
                    // point and its original cloud/source index. Do not emit a
                    // centroid, because downstream picking/export requires a
                    // traceable physical sample.
                    ++result.crossCloudDuplicateCount;
                    continue;
                }
                if (existing == representatives.end())
                    representatives.emplace(key, fusedPoints.size());
            }
            fusedPoints.push_back(point);
            fusedCloudIds.push_back(result.cloudIds[i]);
            fusedSourceIndices.push_back(result.sourceIndices[i]);
        }
        result.points = std::move(fusedPoints);
        result.cloudIds = std::move(fusedCloudIds);
        result.sourceIndices = std::move(fusedSourceIndices);
    }
    result.diagnostics += QStringLiteral(
        "Cross-cloud real-point fusion: before=%1, after=%2, duplicates=%3, voxel=%4 mm\n")
        .arg(result.pointsBeforeCrossCloudFusion).arg(result.points.size())
        .arg(result.crossCloudDuplicateCount).arg(result.crossCloudFusionVoxelSize, 0, 'g', 8);
    result.ok = !result.points.isEmpty();
    if (result.ok && !icp.enabled) writeMergeCache(inputs, result);
    return result;
}

}

NoiseResult removeNoise(const QVector<Point3D> &points, const NoiseOptions &options) {
    NoiseResult result;
    if (points.isEmpty()) {
        result.error = QStringLiteral("没有可处理的点云");
        return result;
    }
    QVector<Point3D> validPoints;
    validPoints.reserve(points.size());
    for (const Point3D &point : points)
        if (usablePoint(point)) validPoints.push_back(point);
    if (validPoints.isEmpty()) {
        result.error = QStringLiteral("点云中没有有效坐标点");
        return result;
    }
    // Denoising runs against the current canvas cache. Radius filtering is
    // intentionally absent; statistical K-neighbour distances determine the
    // threshold from the current cloud's own scale.
    QVector<Point3D> filtered = options.voxelEnabled
        ? voxelFilter(validPoints, options.voxelSize) : validPoints;
    if (options.statisticalEnabled) {
        const float estimatedCell = pcv::detail::filtering::estimateCellSize(filtered);
        const float cell = options.voxelEnabled && options.voxelSize > 0.0f
            ? qMax(options.voxelSize, estimatedCell) : estimatedCell;
        pcv::detail::filtering::StatisticalFilterOptions statisticalOptions;
        statisticalOptions.cellSize = cell;
        statisticalOptions.meanK = options.meanK;
        statisticalOptions.stddevMultiplier = options.stddevMultiplier;
        pcv::detail::filtering::StatisticalFilterResult statistical =
            pcv::detail::filtering::removeStatisticalOutliers(filtered, statisticalOptions);
        filtered = std::move(statistical.points);
        result.error = std::move(statistical.warning);
    }
    result.points = std::move(filtered);
    result.ok = true;
    return result;
}

LoadResult loadPlyResult(const QString &fileName) {
    LoadResult result;
    pcv::detail::io::PlyReadResult parsed = pcv::detail::io::readPly(fileName);
    result.ok = parsed.ok;
    result.error = std::move(parsed.error);
    result.points = std::move(parsed.points);
    result.minimum = parsed.minimum;
    result.maximum = parsed.maximum;
    result.hasBounds = parsed.hasBounds;
    result.headerElapsedMs = parsed.headerElapsedMs;
    result.boundaryScanElapsedMs = parsed.boundaryScanElapsedMs;
    result.parseElapsedMs = parsed.parseElapsedMs;
    result.totalElapsedMs = parsed.totalElapsedMs;
    result.usedCache = false;
    return result;
}

LoadResult loadPlyResultWithProgress(const QString &fileName,
                                     const LoadProgressCallback &progress) {
    LoadResult result;
    pcv::detail::io::PlyReadOptions options;
    options.progress = progress;
    pcv::detail::io::PlyReadResult parsed = pcv::detail::io::readPly(fileName, options);
    result.ok = parsed.ok;
    result.error = std::move(parsed.error);
    result.points = std::move(parsed.points);
    result.minimum = parsed.minimum;
    result.maximum = parsed.maximum;
    result.hasBounds = parsed.hasBounds;
    result.headerElapsedMs = parsed.headerElapsedMs;
    result.boundaryScanElapsedMs = parsed.boundaryScanElapsedMs;
    result.parseElapsedMs = parsed.parseElapsedMs;
    result.totalElapsedMs = parsed.totalElapsedMs;
    result.usedCache = false;
    return result;
}

namespace {

// Jacobi diagonalization for a real symmetric 3x3 matrix.  The returned
// columns are orthonormal eigenvectors; eigenvalues are sorted descending.
void symmetricEigen3(const double input[3][3], double values[3], double vectors[3][3]) {
    double a[3][3];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) a[r][c] = input[r][c];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) vectors[r][c] = (r == c ? 1.0 : 0.0);
    for (int iter = 0; iter < 24; ++iter) {
        int p = 0, q = 1;
        double maxOff = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > maxOff) { p = 0; q = 2; maxOff = std::abs(a[0][2]); }
        if (std::abs(a[1][2]) > maxOff) { p = 1; q = 2; maxOff = std::abs(a[1][2]); }
        if (maxOff < 1.0e-12) break;
        const double phi = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double cs = std::cos(phi), sn = std::sin(phi);
        for (int k = 0; k < 3; ++k) {
            const double apk = a[p][k], aqk = a[q][k];
            a[p][k] = cs * apk - sn * aqk;
            a[q][k] = sn * apk + cs * aqk;
        }
        for (int k = 0; k < 3; ++k) {
            const double akp = a[k][p], akq = a[k][q];
            a[k][p] = cs * akp - sn * akq;
            a[k][q] = sn * akp + cs * akq;
            const double vkp = vectors[k][p], vkq = vectors[k][q];
            vectors[k][p] = cs * vkp - sn * vkq;
            vectors[k][q] = sn * vkp + cs * vkq;
        }
    }
    values[0] = a[0][0]; values[1] = a[1][1]; values[2] = a[2][2];
    // Sort eigenpairs by descending eigenvalue so descriptor formulas are
    // deterministic.  Swap columns of the eigenvector matrix accordingly.
    for (int i = 0; i < 3; ++i) {
        int best = i;
        for (int j = i + 1; j < 3; ++j) if (values[j] > values[best]) best = j;
        if (best == i) continue;
        std::swap(values[i], values[best]);
        for (int r = 0; r < 3; ++r) std::swap(vectors[r][i], vectors[r][best]);
    }
}

} // namespace

GeometryFeatureResult extractGeometryFeatures(const QVector<Point3D> &points,
                                              const GeometryFeatureOptions &options) {
    GeometryFeatureResult result;
    if (points.isEmpty()) {
        result.error = QStringLiteral("没有可处理的点云");
        return result;
    }
    const int requestedK = qMax(1, options.neighborCount != 24 || options.neighbors == 24
                                    ? options.neighborCount : options.neighbors);
    const int minK = qMax(3, options.minNeighbors);
    const float radius = options.searchRadius > 0.0f ? options.searchRadius : options.radius;
    result.features.resize(points.size());
    // 通过均匀空间哈希只搜索相邻网格，将邻域查找从 O(N²) 降到近似
    // O(N·局部密度)；未指定半径时依据点云体积和数量估算网格尺寸。
    float minX = points.first().x, maxX = minX;
    float minY = points.first().y, maxY = minY;
    float minZ = points.first().z, maxZ = minZ;
    for (const Point3D &point : points) {
        minX = qMin(minX, point.x); maxX = qMax(maxX, point.x);
        minY = qMin(minY, point.y); maxY = qMax(maxY, point.y);
        minZ = qMin(minZ, point.z); maxZ = qMax(maxZ, point.z);
    }
    const float spanX = qMax(maxX - minX, 1.0e-4f);
    const float spanY = qMax(maxY - minY, 1.0e-4f);
    const float spanZ = qMax(maxZ - minZ, 1.0e-4f);
    const float spacing = std::cbrt(double(spanX * spanY * spanZ)
                                    / qMax(1, points.size()));
    const float cellSize = radius > 0.0f ? radius : qMax(spacing * 2.5f, 1.0e-4f);
    std::unordered_map<NoiseKey, std::vector<int>, NoiseKeyHash> grid;
    grid.reserve(size_t(points.size() * 1.3));
    for (int i = 0; i < points.size(); ++i) grid[noiseKey(points[i], cellSize)].push_back(i);
    QVector<std::pair<float, int>> distances;
    distances.reserve(requestedK * 8);
    for (int i = 0; i < points.size(); ++i) {
        distances.clear();
        const Point3D &p = points[i];
        const NoiseKey key = noiseKey(p, cellSize);
        for (qint64 dz = -1; dz <= 1; ++dz) for (qint64 dy = -1; dy <= 1; ++dy)
            for (qint64 dx = -1; dx <= 1; ++dx) {
                const auto it = grid.find({key.x + dx, key.y + dy, key.z + dz});
                if (it == grid.end()) continue;
                for (int j : it->second) {
                    if (j == i) continue;
                    const Point3D &q = points[j];
                    const float ddx = p.x - q.x, ddy = p.y - q.y, ddz = p.z - q.z;
                    const float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                    if (radius > 0.0f && d2 > radius * radius) continue;
                    distances.push_back({d2, j});
                }
            }
        // 点云过稀或各向异性很强时，邻域可能跨越多个网格；对中小规模
        // 数据启用一次受限全扫描，优先保证特征计算的正确性。
        if (distances.size() < minK && points.size() <= 200000) {
            distances.clear();
            for (int j = 0; j < points.size(); ++j) {
                if (j == i) continue;
                const Point3D &q = points[j];
                const float ddx = p.x - q.x, ddy = p.y - q.y, ddz = p.z - q.z;
                const float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                if (radius <= 0.0f || d2 <= radius * radius) distances.push_back({d2, j});
            }
        }
        if (distances.size() < minK) continue;
        const int count = qMin(requestedK, int(distances.size()));
        if (count < distances.size()) {
            std::nth_element(distances.begin(), distances.begin() + count, distances.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
        }
        distances.resize(count);
        double mean[3] = {0.0, 0.0, 0.0};
        for (const auto &entry : distances) {
            const Point3D &q = points[entry.second];
            mean[0] += q.x; mean[1] += q.y; mean[2] += q.z;
        }
        const double inv = 1.0 / double(count);
        mean[0] *= inv; mean[1] *= inv; mean[2] *= inv;
        double cov[3][3] = {};
        for (const auto &entry : distances) {
            const Point3D &q = points[entry.second];
            const double d[3] = {q.x - mean[0], q.y - mean[1], q.z - mean[2]};
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) cov[r][c] += d[r] * d[c] * inv;
        }
        double eig[3], vec[3][3];
        symmetricEigen3(cov, eig, vec);
        eig[0] = qMax(0.0, eig[0]); eig[1] = qMax(0.0, eig[1]); eig[2] = qMax(0.0, eig[2]);
        const double scale = eig[0] > 1.0e-15 ? eig[0] : 0.0;
        GeometryFeature &f = result.features[i];
        f.lambda1 = float(eig[0]); f.lambda2 = float(eig[1]); f.lambda3 = float(eig[2]);
        f.neighborCount = count;
        if (scale > 0.0) {
            f.linearity = float((eig[0] - eig[1]) / eig[0]);
            f.planarity = float((eig[1] - eig[2]) / eig[0]);
            f.scattering = float(eig[2] / eig[0]);
            f.curvature = float(eig[2] / (eig[0] + eig[1] + eig[2]));
        }
        double nx = vec[0][2], ny = vec[1][2], nz = vec[2][2];
        if (options.useExistingNormals) {
            const double ex = p.nx, ey = p.ny, ez = p.nz;
            const double n2 = ex * ex + ey * ey + ez * ez;
            if (n2 > 1.0e-12) { const double s = 1.0 / std::sqrt(n2); nx = ex * s; ny = ey * s; nz = ez * s; }
        }
        f.nx = float(nx); f.ny = float(ny); f.nz = float(nz);
        f.valid = true;
    }
    int validCount = 0;
    for (const GeometryFeature &f : result.features) if (f.valid) ++validCount;
    result.ok = validCount > 0;
    if (!result.ok) result.error = QStringLiteral("邻域点数不足，无法提取几何特征");
    if (result.ok) {
        double curvature = 0.0, linearity = 0.0, planarity = 0.0, scattering = 0.0;
        for (const GeometryFeature &f : result.features) if (f.valid) {
            curvature += f.curvature; linearity += f.linearity;
            planarity += f.planarity; scattering += f.scattering;
        }
        const double n = double(validCount);
        result.summary = QStringLiteral("局部 PCA 特征提取完成\n有效点：%1 / %2\n邻域 K：%3\n平均曲率：%4\n平均线性度：%5\n平均平面度：%6\n平均散乱度：%7\n法向量：已估计（最小特征值方向）")
            .arg(QLocale().toString(validCount))
            .arg(QLocale().toString(points.size()))
            .arg(requestedK)
            .arg(curvature / n, 0, 'f', 5)
            .arg(linearity / n, 0, 'f', 5)
            .arg(planarity / n, 0, 'f', 5)
            .arg(scattering / n, 0, 'f', 5);
    }
    return result;
}

FeatureResult extractFeatures(const QVector<Point3D> &points, const FeatureOptions &options) {
    FeatureResult out;
    out.sourceCount = points.size();
    const QVector<Point3D> sampled = proportionalDownsample(points,
                                                            qMax(1, options.downsampleDenominator));
    out.inputCount = sampled.size();
    GeometryFeatureOptions local;
    local.neighborCount = options.neighbors;
    local.searchRadius = options.radius;
    local.useExistingNormals = !options.estimateNormals;
    const GeometryFeatureResult detailed = extractGeometryFeatures(sampled, local);
    out.ok = detailed.ok;
    out.error = detailed.error;
    out.curvature.resize(sampled.size());
    out.linearity.resize(sampled.size());
    out.planarity.resize(sampled.size());
    out.sphericity.resize(sampled.size());
    out.roughness.resize(sampled.size());
    int valid = 0;
    double curvatureSum = 0.0;
    for (int i = 0; i < detailed.features.size(); ++i) {
        const GeometryFeature &f = detailed.features[i];
        out.curvature[i] = f.curvature;
        out.linearity[i] = f.linearity;
        out.planarity[i] = f.planarity;
        out.sphericity[i] = f.scattering;
        out.roughness[i] = f.curvature;
        if (f.valid) { ++valid; curvatureSum += f.curvature; }
    }
    if (out.ok) {
        const double meanCurvature = valid > 0 ? curvatureSum / valid : 0.0;
        out.summary = QStringLiteral("已完成局部 PCA 特征提取：%1 点，有效邻域 %2，平均曲率 %3")
                          .arg(sampled.size()).arg(valid).arg(meanCurvature, 0, 'g', 4);
    }
    return out;
}

CompletionResult completeGeometry(const QVector<Point3D> &points,
                                   const GeometryFeatureResult *features,
                                   const CompletionOptions &options) {
    CompletionResult result;
    if (points.size() < options.minCirclePoints) {
        result.error = QStringLiteral("点数不足，无法进行几何补全");
        return result;
    }
    QVector<Point3D> source;
    source.reserve(points.size());
    for (const Point3D &p : points) if (usablePoint(p)) source.push_back(p);
    if (source.size() < options.minCirclePoints) {
        result.error = QStringLiteral("有效点不足，无法进行几何补全");
        return result;
    }
    // 先用 PCA 拟合支撑平面，再取平面内两个正交特征向量作为二维坐标轴，
    // 后续圆弧检测和补点都在该局部坐标系中完成，生成点再映射回三维。
    double mean[3] = {};
    for (const Point3D &p : source) { mean[0] += p.x; mean[1] += p.y; mean[2] += p.z; }
    for (double &v : mean) v /= source.size();
    double cov[3][3] = {};
    for (const Point3D &p : source) {
        const double d[3] = {p.x - mean[0], p.y - mean[1], p.z - mean[2]};
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) cov[r][c] += d[r] * d[c];
    }
    double eig[3], vec[3][3];
    symmetricEigen3(cov, eig, vec);
    const QVector3D u{float(vec[0][0]), float(vec[1][0]), float(vec[2][0])};
    const QVector3D v{float(vec[0][1]), float(vec[1][1]), float(vec[2][1])};
    QVector3D n = QVector3D::crossProduct(u, v).normalized();
    if (u.lengthSquared() < 1.0e-8f || v.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("点云几何退化，无法建立补全平面");
        return result;
    }
    QVector<std::pair<float, float>> polar;
    polar.reserve(source.size());
    const QVector3D origin{float(mean[0]), float(mean[1]), float(mean[2])};
    for (const Point3D &p : source) {
        const QVector3D q(p.x, p.y, p.z); const QVector3D d = q - origin;
        const float x = QVector3D::dotProduct(d, u), y = QVector3D::dotProduct(d, v);
        polar.push_back({std::sqrt(x * x + y * y), std::atan2(y, x)});
    }
    // 以半径中位数作为圆周半径，对离群半径点保持鲁棒性。
    std::sort(polar.begin(), polar.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const float radius = polar[polar.size() / 2].first;
    if (radius <= 1.0e-5f) { result.error = QStringLiteral("未检测到有效圆形半径"); return result; }
    const float tolerance = qMax(options.radialTolerance * radius, 1.0e-4f);
    QVector<float> circleAngles;
    for (const auto &entry : polar)
        if (std::abs(entry.first - radius) <= tolerance) circleAngles.push_back(entry.second);
    if (circleAngles.size() < options.minCirclePoints) {
        result.error = QStringLiteral("圆形候选点不足，未执行补全");
        return result;
    }
    std::sort(circleAngles.begin(), circleAngles.end());
    const float pi2 = float(2.0 * qAcos(-1.0));
    QVector<Point3D> generated;
    const int samples = qMax(1, options.samplesPerGap);
    const float maxGap = qDegreesToRadians(qBound(1.0f, options.maxGapAngleDegrees, 180.0f));
    // 相邻角度超过阈值即视为缺口，在缺口内部均匀插值生成圆弧点。
    for (int i = 0; i < circleAngles.size(); ++i) {
        float a = circleAngles[i], b = (i + 1 < circleAngles.size()) ? circleAngles[i + 1] : circleAngles[0] + pi2;
        const float gap = b - a;
        if (gap <= maxGap) continue;
        for (int s = 1; s <= samples; ++s) {
            const float t = float(s) / float(samples + 1);
            const float angle = a + gap * t;
            const QVector3D q = origin + u * (radius * std::cos(angle)) + v * (radius * std::sin(angle));
            Point3D p{q.x(), q.y(), q.z(), n.x(), n.y(), n.z()};
            generated.push_back(p);
        }
    }
    result.points = source;
    result.points += generated;
    result.generatedCount = generated.size();
    result.circleCount = generated.isEmpty() ? 0 : 1;
    result.ok = true;
    result.summary = QStringLiteral("几何补全完成：识别圆形 1 个，补充 %1 点；平面法向连续，边缘按圆弧平滑连接")
        .arg(generated.size());
    Q_UNUSED(features);
    Q_UNUSED(options.smoothEdges);
    return result;
}

ChamferResult completeChamfers(const QVector<Point3D> &points,
                               const ChamferOptions &options) {
    ChamferResult result;
    if (points.size() < 6) {
        result.error = QStringLiteral("点数不足，无法检测倒角");
        return result;
    }
    PlaneSegmentationOptions planeOptions;
    planeOptions.distanceThreshold = qMax(1.0e-5f, options.distanceThreshold);
    planeOptions.maxPlanes = qMax(2, options.maxCandidates * 2);
    planeOptions.iterations = 300;
    planeOptions.minInliers = qMax(options.minSupportPoints, 12);
    planeOptions.sampleDenominator = 1;
    planeOptions.preferHorizontal = false;
    const PlaneSegmentationResult planes = segmentPlanes(points, planeOptions);
    if (planes.planes.size() < 2) {
        result.error = QStringLiteral("未检测到足够的相邻支撑平面");
        return result;
    }
    const float threshold = planeOptions.distanceThreshold;
    QVector<Point3D> generated;

    struct EdgeSample {
        QVector3D offset;
        float along = 0.0f;
        float oppositeDistance = 0.0f;
    };
    const auto percentile = [](QVector<float> values, float ratio) {
        if (values.isEmpty()) return 0.0f;
        std::sort(values.begin(), values.end());
        const int index = qBound(0, int(std::floor(ratio * (values.size() - 1))),
                                 values.size() - 1);
        return values[index];
    };
    const auto robustRange = [&percentile](const QVector<EdgeSample> &samples) {
        QVector<float> values;
        values.reserve(samples.size());
        for (const EdgeSample &sample : samples) values.push_back(sample.along);
        return std::pair<float, float>{percentile(values, 0.02f),
                                       percentile(values, 0.98f)};
    };

    // 用空间哈希同时过滤原始点和多个候选倒角之间的重复采样点。
    const float duplicateTolerance = qMax(threshold * 0.5f, 1.0e-5f);
    const float duplicateToleranceSquared = duplicateTolerance * duplicateTolerance;
    std::unordered_map<NoiseKey, std::vector<QVector3D>, NoiseKeyHash> occupied;
    occupied.reserve(size_t(points.size() + 1024));
    for (const Point3D &point : points)
        occupied[noiseKey(point, duplicateTolerance)].push_back(
            QVector3D(point.x, point.y, point.z));
    const auto appendUnique = [&](const QVector3D &position, const QVector3D &normal) {
        const Point3D point{position.x(), position.y(), position.z(),
                            normal.x(), normal.y(), normal.z()};
        const NoiseKey key = noiseKey(point, duplicateTolerance);
        for (qint64 dz = -1; dz <= 1; ++dz)
            for (qint64 dy = -1; dy <= 1; ++dy)
                for (qint64 dx = -1; dx <= 1; ++dx) {
                    const auto found = occupied.find({key.x + dx, key.y + dy, key.z + dz});
                    if (found == occupied.end()) continue;
                    for (const QVector3D &existing : found->second)
                        if ((existing - position).lengthSquared() <= duplicateToleranceSquared)
                            return false;
                }
        occupied[key].push_back(position);
        generated.push_back(point);
        return true;
    };

    for (int ia = 0; ia < planes.planes.size() && result.candidates.size() < options.maxCandidates; ++ia) {
        for (int ib = ia + 1; ib < planes.planes.size() && result.candidates.size() < options.maxCandidates; ++ib) {
            const PlaneModel &a = planes.planes[ia];
            const PlaneModel &b = planes.planes[ib];
            QVector3D na(a.a, a.b, a.c), nb(b.a, b.b, b.c);
            na.normalize();
            nb.normalize();
            const float angle = qRadiansToDegrees(std::acos(qBound(-1.0f,
                std::abs(QVector3D::dotProduct(na, nb)), 1.0f)));
            if (angle < options.minAngleDegrees || angle > options.maxAngleDegrees) continue;
            QVector3D direction = QVector3D::crossProduct(na, nb);
            const float directionSquared = direction.lengthSquared();
            if (directionSquared < 1.0e-8f) continue;

            // 两平面方程均已归一化；该闭式解给出离坐标原点最近的交线点。
            const QVector3D origin = QVector3D::crossProduct(
                b.d * na - a.d * nb, direction) / directionSquared;
            const QVector3D axis = direction.normalized();
            const QVector3D acrossA = QVector3D::crossProduct(axis, na).normalized();
            const QVector3D acrossB = QVector3D::crossProduct(axis, nb).normalized();
            QVector<EdgeSample> samplesA;
            QVector<EdgeSample> samplesB;
            samplesA.reserve(a.inlierCount);
            samplesB.reserve(b.inlierCount);
            float minAcrossA = std::numeric_limits<float>::max();
            float maxAcrossA = -minAcrossA;
            float minAcrossB = std::numeric_limits<float>::max();
            float maxAcrossB = -minAcrossB;
            float minAlongA = std::numeric_limits<float>::max();
            float maxAlongA = -minAlongA;
            float minAlongB = std::numeric_limits<float>::max();
            float maxAlongB = -minAlongB;
            for (int pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
                const int label = planes.labels.value(pointIndex, -1);
                if (label != ia && label != ib) continue;
                const Point3D &point = points[pointIndex];
                const QVector3D delta = QVector3D(point.x, point.y, point.z) - origin;
                const float along = QVector3D::dotProduct(delta, axis);
                const QVector3D offset = delta - axis * along;
                if (label == ia) {
                    const float across = QVector3D::dotProduct(offset, acrossA);
                    samplesA.push_back({offset, along,
                        std::abs(QVector3D::dotProduct(nb, QVector3D(point.x, point.y, point.z))
                                 + b.d)});
                    minAcrossA = qMin(minAcrossA, across); maxAcrossA = qMax(maxAcrossA, across);
                    minAlongA = qMin(minAlongA, along); maxAlongA = qMax(maxAlongA, along);
                } else {
                    const float across = QVector3D::dotProduct(offset, acrossB);
                    samplesB.push_back({offset, along,
                        std::abs(QVector3D::dotProduct(na, QVector3D(point.x, point.y, point.z))
                                 + a.d)});
                    minAcrossB = qMin(minAcrossB, across); maxAcrossB = qMax(maxAcrossB, across);
                    minAlongB = qMin(minAlongB, along); maxAlongB = qMax(maxAlongB, along);
                }
            }
            const int requiredPerPlane = qMax(4, options.minSupportPoints / 2);
            if (samplesA.size() < requiredPerPlane || samplesB.size() < requiredPerPlane) continue;

            const float areaA = qMax(threshold * threshold,
                (maxAcrossA - minAcrossA) * (maxAlongA - minAlongA));
            const float areaB = qMax(threshold * threshold,
                (maxAcrossB - minAcrossB) * (maxAlongB - minAlongB));
            const float spacingA = std::sqrt(areaA / qMax(1, samplesA.size()));
            const float spacingB = std::sqrt(areaB / qMax(1, samplesB.size()));
            const float cloudSpacing = qMax(threshold, 0.5f * (spacingA + spacingB));

            QVector<float> distancesA;
            QVector<float> distancesB;
            distancesA.reserve(samplesA.size());
            distancesB.reserve(samplesB.size());
            for (const EdgeSample &sample : samplesA) distancesA.push_back(sample.oppositeDistance);
            for (const EdgeSample &sample : samplesB) distancesB.push_back(sample.oppositeDistance);
            const float setbackA = percentile(distancesA, 0.01f);
            const float setbackB = percentile(distancesB, 0.01f);
            const float minimumGap = qMax(threshold * 3.0f, qMin(spacingA, spacingB) * 1.25f);
            const float maximumAutoGap = qMax(cloudSpacing * 8.0f,
                qMin(maxAcrossA - minAcrossA, maxAcrossB - minAcrossB) * 0.15f);
            if (options.width <= 0.0f
                && (qMin(setbackA, setbackB) <= minimumGap
                    || qMax(setbackA, setbackB) > maximumAutoGap))
                continue;

            const float bandA = setbackA + qMax(threshold * 2.0f, spacingA * 0.55f);
            const float bandB = setbackB + qMax(threshold * 2.0f, spacingB * 0.55f);
            QVector<EdgeSample> edgeA;
            QVector<EdgeSample> edgeB;
            QVector3D offsetA;
            QVector3D offsetB;
            for (const EdgeSample &sample : samplesA) {
                if (sample.oppositeDistance > bandA) continue;
                edgeA.push_back(sample);
                offsetA += sample.offset;
            }
            for (const EdgeSample &sample : samplesB) {
                if (sample.oppositeDistance > bandB) continue;
                edgeB.push_back(sample);
                offsetB += sample.offset;
            }
            if (edgeA.size() < requiredPerPlane || edgeB.size() < requiredPerPlane) continue;
            offsetA /= float(edgeA.size());
            offsetB /= float(edgeB.size());
            offsetA -= na * QVector3D::dotProduct(offsetA, na);
            offsetB -= nb * QVector3D::dotProduct(offsetB, nb);

            // 仅在两侧边界都有观测覆盖的交线区间内生成，防止补全越过工件端面。
            const auto rangeA = robustRange(edgeA);
            const auto rangeB = robustRange(edgeB);
            const float minT = qMax(rangeA.first, rangeB.first);
            const float maxT = qMin(rangeA.second, rangeB.second);
            if (maxT - minT < cloudSpacing * 2.0f) continue;

            const QVector3D crossStrip = offsetB - offsetA;
            const float stripWidth = crossStrip.length();
            if (stripWidth <= threshold) continue;
            QVector3D chamferNormal = QVector3D::crossProduct(axis, crossStrip).normalized();
            QVector3D referenceNormal = na;
            if (QVector3D::dotProduct(referenceNormal, nb) < 0.0f) referenceNormal -= nb;
            else referenceNormal += nb;
            if (QVector3D::dotProduct(chamferNormal, referenceNormal) < 0.0f)
                chamferNormal = -chamferNormal;
            QVector3D roundCenterOffset;
            QVector3D roundRadiusA;
            float roundRadius = 0.0f;
            float roundSweep = 0.0f;
            if (options.profileType == ChamferProfileType::Round) {
                // R 倒角圆心必须同时位于两个端点的支撑面法线上，这是切线连续的必要条件。
                const float normalCrossSquared = QVector3D::crossProduct(na, nb).lengthSquared();
                if (normalCrossSquared <= 1.0e-8f) continue;
                const float centerDistanceA = QVector3D::dotProduct(
                    QVector3D::crossProduct(offsetB - offsetA, nb), axis)
                    / normalCrossSquared;
                roundCenterOffset = offsetA + na * centerDistanceA;
                roundRadiusA = offsetA - roundCenterOffset;
                const QVector3D roundRadiusB = offsetB - roundCenterOffset;
                const float radiusA = roundRadiusA.length();
                const float radiusB = roundRadiusB.length();
                if (qMin(radiusA, radiusB) <= threshold
                    || std::abs(radiusA - radiusB) / qMax(radiusA, radiusB) > 0.25f)
                    continue;
                roundRadius = 0.5f * (radiusA + radiusB);
                roundRadiusA = roundRadiusA.normalized() * roundRadius;
                const QVector3D normalizedRadiusB = roundRadiusB.normalized() * roundRadius;
                roundSweep = std::atan2(
                    QVector3D::dotProduct(axis,
                        QVector3D::crossProduct(roundRadiusA, normalizedRadiusB)),
                    QVector3D::dotProduct(roundRadiusA, normalizedRadiusB));
                if (std::abs(roundSweep) < qDegreesToRadians(5.0f)) continue;
            }

            const QVector3D center = origin + 0.5f * (offsetA + offsetB);
            PlaneModel chamfer;
            chamfer.a = chamferNormal.x(); chamfer.b = chamferNormal.y(); chamfer.c = chamferNormal.z();
            chamfer.d = -QVector3D::dotProduct(chamferNormal, center);
            ChamferCandidate candidate;
            candidate.planeA = ia; candidate.planeB = ib; candidate.model = chamfer;
            candidate.lineOrigin = {origin.x(), origin.y(), origin.z()};
            candidate.lineDirection = {axis.x(), axis.y(), axis.z()};
            candidate.angleDegrees = qRadiansToDegrees(std::acos(qBound(-1.0f,
                std::abs(QVector3D::dotProduct(chamferNormal, na)), 1.0f)));
            candidate.width = stripWidth;
            candidate.supportCount = edgeA.size() + edgeB.size();
            const float symmetry = qMin(setbackA, setbackB)
                                   / qMax(qMax(setbackA, setbackB), threshold);
            const float supportScore = qMin(1.0f,
                float(candidate.supportCount) / qMax(1, options.minSupportPoints * 4));
            candidate.confidence = qBound(0.0f, 0.55f * symmetry + 0.45f * supportScore, 1.0f);
            candidate.radius = roundRadius;
            candidate.planeRms = qMax(a.meanDistance, b.meanDistance);
            const float pixelSigma = cloudSpacing / std::sqrt(12.0f);
            const float sampleSigma = cloudSpacing / std::sqrt(float(qMax(1, candidate.supportCount)));
            candidate.uncertaintyWidth = qMax(0.08f, 2.0f * std::sqrt(
                pixelSigma * pixelSigma + sampleSigma * sampleSigma
                + candidate.planeRms * candidate.planeRms));
            candidate.uncertaintyAngle = qMax(1.5f,
                qRadiansToDegrees(std::atan2(candidate.uncertaintyWidth,
                                             qMax(stripWidth, threshold))));
            candidate.profileType = options.profileType;
            candidate.reviewStatus = candidate.confidence >= 0.75f
                                         && candidate.planeRms <= 0.10f
                ? FeatureReviewStatus::Measurable : FeatureReviewStatus::NeedsReview;
            result.candidates.push_back(candidate);

            const float sampleSpacing = options.sampleSpacing > 0.0f
                ? options.sampleSpacing : cloudSpacing;
            const int alongCount = qBound(2, int(std::ceil((maxT - minT) / sampleSpacing)), 4096);
            const int acrossCount = qBound(2, int(std::ceil(stripWidth / sampleSpacing)), 64);
            for (int alongIndex = 0; alongIndex <= alongCount; ++alongIndex) {
                const float along = minT + (maxT - minT) * float(alongIndex) / float(alongCount);
                for (int acrossIndex = 0; acrossIndex <= acrossCount; ++acrossIndex) {
                    const float blend = float(acrossIndex) / float(acrossCount);
                    QVector3D offset;
                    QVector3D sampleNormal = chamferNormal;
                    if (options.profileType == ChamferProfileType::Round) {
                        const float rotation = roundSweep * blend;
                        const QVector3D radiusVector = roundRadiusA * std::cos(rotation)
                            + QVector3D::crossProduct(axis, roundRadiusA) * std::sin(rotation);
                        offset = roundCenterOffset + radiusVector;
                        sampleNormal = radiusVector.normalized();
                        if (QVector3D::dotProduct(sampleNormal, referenceNormal) < 0.0f)
                            sampleNormal = -sampleNormal;
                    } else {
                        offset = offsetA * (1.0f - blend) + offsetB * blend;
                    }
                    if (appendUnique(origin + axis * along + offset, sampleNormal)) {
                        result.generatedMetadata.push_back({
                            int(result.candidates.size()) - 1,
                            PointSourceType::ParametricFill,
                            candidate.confidence,
                            candidate.uncertaintyWidth,
                            int(generated.size()) - 1
                        });
                    }
                }
            }
        }
    }
    result.points = points;
    result.points += generated;
    result.generatedPoints = generated;
    result.generatedCount = generated.size();
    result.ok = !result.candidates.isEmpty();
    if (result.ok) {
        result.summary = QStringLiteral("倒角拟合预览：候选 %1 个，参数化点 %2；等待人工确认")
            .arg(result.candidates.size()).arg(result.generatedCount);
    } else {
        result.error = QStringLiteral("未找到满足角度和支撑点条件的倒角");
    }
    return result;
}

CircleDetectionResult detectCircleOnPlane(const QVector<Point3D> &points,
                                          const PlaneModel &plane,
                                          const CircleDetectionOptions &options) {
    CircleDetectionResult result;
    if (points.size() < 3) {
        result.error = QStringLiteral("框选区域点数不足，至少需要 3 个点");
        return result;
    }

    // 所有输入点先正交投影到确认平面，避免原始 z 抖动影响二维圆拟合。
    QVector3D normal(plane.a, plane.b, plane.c);
    const float normalLength = normal.length();
    if (normalLength <= 1.0e-8f) {
        result.error = QStringLiteral("目标平面法向无效");
        return result;
    }
    normal /= normalLength;
    const QVector3D helper = std::abs(normal.z()) < 0.9f
        ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
    const QVector3D u = QVector3D::crossProduct(helper, normal).normalized();
    const QVector3D v = QVector3D::crossProduct(normal, u).normalized();
    const QVector3D origin = -(plane.d / normalLength) * normal;

    struct Sample2D { double x; double y; int sourceIndex; };
    QVector<Sample2D> samples;
    samples.reserve(points.size());
    double minX = std::numeric_limits<double>::max();
    double minY = minX;
    double maxX = -minX;
    double maxY = -minX;
    for (int i = 0; i < points.size(); ++i) {
        const Point3D &point = points[i];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            continue;
        const QVector3D delta = QVector3D(point.x, point.y, point.z) - origin;
        const double x = QVector3D::dotProduct(delta, u);
        const double y = QVector3D::dotProduct(delta, v);
        samples.push_back({x, y, i});
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    if (samples.size() < 3) {
        result.error = QStringLiteral("框选区域没有足够的有效点");
        return result;
    }

    const double diagonal = std::hypot(maxX - minX, maxY - minY);
    if (diagonal <= 1.0e-7) {
        result.error = QStringLiteral("框选区域几何范围过小");
        return result;
    }
    const double tolerance = options.radialTolerance > 0.0f
        ? options.radialTolerance : std::max(diagonal * 0.0125, 1.0e-4);
    const int angularBins = qBound(12, options.angularBins, 180);
    const int requiredBins = qBound(6, options.minOccupiedBins, angularBins);
    const int requiredInliers = qBound(6, options.minInliers, int(samples.size()));
    const double minRadius = diagonal * 0.025;
    const double maxRadius = diagonal * 10.0;

    auto circleFromThree = [](const Sample2D &a, const Sample2D &b, const Sample2D &c,
                              double &cx, double &cy, double &radius) {
        const double denominator = 2.0 * (a.x * (b.y - c.y)
                                          + b.x * (c.y - a.y)
                                          + c.x * (a.y - b.y));
        if (std::abs(denominator) <= 1.0e-12) return false;
        const double aa = a.x * a.x + a.y * a.y;
        const double bb = b.x * b.x + b.y * b.y;
        const double cc = c.x * c.x + c.y * c.y;
        cx = (aa * (b.y - c.y) + bb * (c.y - a.y) + cc * (a.y - b.y))
             / denominator;
        cy = (aa * (c.x - b.x) + bb * (a.x - c.x) + cc * (b.x - a.x))
             / denominator;
        radius = std::hypot(a.x - cx, a.y - cy);
        return std::isfinite(radius);
    };

    std::mt19937 rng(options.randomSeed);
    std::uniform_int_distribution<int> pick(0, samples.size() - 1);
    QVector<int> bestInliers;
    int bestBins = 0;
    int bestScore = -1;
    double bestCx = 0.0, bestCy = 0.0, bestRadius = 0.0;
    const double pi2 = 2.0 * std::acos(-1.0);
    for (int iteration = 0; iteration < qMax(100, options.iterations); ++iteration) {
        const int ia = pick(rng), ib = pick(rng), ic = pick(rng);
        if (ia == ib || ia == ic || ib == ic) continue;
        double cx = 0.0, cy = 0.0, radius = 0.0;
        if (!circleFromThree(samples[ia], samples[ib], samples[ic], cx, cy, radius)
            || radius < minRadius || radius > maxRadius)
            continue;
        QVector<int> inliers;
        QVector<quint8> bins(angularBins, 0);
        for (int i = 0; i < samples.size(); ++i) {
            const double residual = std::abs(std::hypot(samples[i].x - cx, samples[i].y - cy)
                                             - radius);
            if (residual > tolerance) continue;
            inliers.push_back(i);
            double angle = std::atan2(samples[i].y - cy, samples[i].x - cx);
            if (angle < 0.0) angle += pi2;
            bins[qMin(angularBins - 1, int(angle / pi2 * angularBins))] = 1;
        }
        const int occupied = std::count(bins.cbegin(), bins.cend(), quint8(1));
        const int score = inliers.size() + occupied * 4;
        if (occupied >= requiredBins && score > bestScore) {
            bestScore = score;
            bestBins = occupied;
            bestInliers = inliers;
            bestCx = cx; bestCy = cy; bestRadius = radius;
        }
    }
    if (bestInliers.size() < requiredInliers) {
        result.error = QStringLiteral("框选区域内未识别到覆盖充分的圆周，请缩小框选范围或包含更多圆弧");
        return result;
    }

    // Algebraic least-squares refinement: x^2+y^2+A*x+B*y+C=0.
    double matrix[3][4] = {};
    for (int index : bestInliers) {
        const double x = samples[index].x, y = samples[index].y;
        const double q = x * x + y * y;
        matrix[0][0] += x * x; matrix[0][1] += x * y; matrix[0][2] += x; matrix[0][3] -= x * q;
        matrix[1][0] += x * y; matrix[1][1] += y * y; matrix[1][2] += y; matrix[1][3] -= y * q;
        matrix[2][0] += x;     matrix[2][1] += y;     matrix[2][2] += 1.0; matrix[2][3] -= q;
    }
    bool solvable = true;
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row)
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
        if (std::abs(matrix[pivot][column]) <= 1.0e-12) { solvable = false; break; }
        if (pivot != column)
            for (int j = column; j < 4; ++j) std::swap(matrix[pivot][j], matrix[column][j]);
        const double divisor = matrix[column][column];
        for (int j = column; j < 4; ++j) matrix[column][j] /= divisor;
        for (int row = 0; row < 3; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (int j = column; j < 4; ++j) matrix[row][j] -= factor * matrix[column][j];
        }
    }
    if (solvable) {
        const double a = matrix[0][3], b = matrix[1][3], c = matrix[2][3];
        const double radiusSquared = 0.25 * (a * a + b * b) - c;
        if (radiusSquared > 1.0e-12) {
            bestCx = -0.5 * a;
            bestCy = -0.5 * b;
            bestRadius = std::sqrt(radiusSquared);
        }
    }

    QVector<int> refinedInliers;
    QVector<quint8> refinedBins(angularBins, 0);
    double squaredError = 0.0;
    for (int i = 0; i < samples.size(); ++i) {
        const double residual = std::abs(std::hypot(samples[i].x - bestCx,
                                                    samples[i].y - bestCy) - bestRadius);
        if (residual > tolerance) continue;
        refinedInliers.push_back(samples[i].sourceIndex);
        squaredError += residual * residual;
        double angle = std::atan2(samples[i].y - bestCy, samples[i].x - bestCx);
        if (angle < 0.0) angle += pi2;
        refinedBins[qMin(angularBins - 1, int(angle / pi2 * angularBins))] = 1;
    }
    const int refinedOccupied = std::count(refinedBins.cbegin(), refinedBins.cend(), quint8(1));
    if (refinedInliers.size() < requiredInliers || refinedOccupied < requiredBins) {
        result.error = QStringLiteral("圆周细化后有效覆盖不足，请重新框选圆边缘");
        return result;
    }

    const QVector3D center = origin + u * float(bestCx) + v * float(bestCy);
    result.center = {center.x(), center.y(), center.z(), normal.x(), normal.y(), normal.z()};
    result.radius = float(bestRadius);
    result.rmsError = float(std::sqrt(squaredError / refinedInliers.size()));
    result.inlierIndices = refinedInliers;
    result.occupiedBins = refinedOccupied;
    result.angularBinCount = angularBins;
    result.angularCoverage = angularBins > 0
        ? float(refinedOccupied) / float(angularBins) : 0.0f;
    result.coverageDegrees = result.angularCoverage * 360.0f;
    result.inlierRatio = float(refinedInliers.size()) / float(qMax(1, samples.size()));
    const float coverageConfidence = qBound(0.0f, result.angularCoverage, 1.0f);
    const float fitConfidence = std::exp(-result.rmsError /
                                         qMax(1.0e-6f, options.radialTolerance > 0.0f
                                                  ? options.radialTolerance : result.radius * 0.02f));
    result.confidence = qBound(0.0f, 0.55f * coverageConfidence + 0.45f * fitConfidence, 1.0f);
    // 按 95% 扩展不确定度报告；覆盖角越小，半径和圆心的不确定度按 1.5 次幂放大。
    const float baseSigma = std::max({float(tolerance), plane.meanDistance, 0.01f});
    const float coverageSigma = baseSigma * std::pow(
        180.0f / qMax(result.coverageDegrees, 1.0f), 1.5f);
    const float edgeSigma = baseSigma
                            / std::sqrt(float(std::max<qsizetype>(1, refinedInliers.size())));
    result.uncertaintyRadius = qMax(0.04f, 2.0f * std::sqrt(
        baseSigma * baseSigma + coverageSigma * coverageSigma
        + result.rmsError * result.rmsError + edgeSigma * edgeSigma));
    result.uncertaintyCenter = qMax(0.04f, result.uncertaintyRadius
        * (1.0f + 0.5f * (1.0f - result.angularCoverage)));
    result.reviewStatus = result.coverageDegrees >= 108.0f
                              && result.rmsError <= float(tolerance)
                              && plane.meanDistance <= 0.10f
        ? FeatureReviewStatus::Measurable : FeatureReviewStatus::NeedsReview;
    result.ok = true;
    result.summary = QStringLiteral("圆拟合待确认：半径 %1 mm，覆盖 %2°，RMS %3 mm，U(r)=%4 mm (k=2)")
        .arg(result.radius, 0, 'f', 4)
        .arg(result.coverageDegrees, 0, 'f', 1)
        .arg(result.rmsError, 0, 'f', 5)
        .arg(result.uncertaintyRadius, 0, 'f', 4);
    return result;
}

CircleInteriorCleanupResult cleanCircleInterior(
    const QVector<Point3D> &points,
    const PlaneModel &plane,
    const CircleDetectionResult &circle,
    const CircleInteriorCleanupOptions &options) {
    CircleInteriorCleanupResult result;
    if (!circle.ok) {
        result.error = QStringLiteral("圆拟合结果无效");
        return result;
    }
    const float radius = options.radius > 0.0f ? options.radius : circle.radius;
    if (!std::isfinite(radius) || radius <= 1.0e-6f) {
        result.error = QStringLiteral("圆半径必须大于零");
        return result;
    }

    QVector3D normal(plane.a, plane.b, plane.c);
    const float normalLength = normal.length();
    if (normalLength <= 1.0e-8f) {
        result.error = QStringLiteral("目标平面法向无效");
        return result;
    }
    normal /= normalLength;
    const QVector3D center(circle.center.x, circle.center.y, circle.center.z);
    const float protectionWidth = qBound(
        0.0f, options.edgeProtectionWidth, radius * 0.45f);
    const float protectedInteriorRadius = radius - protectionWidth;
    const float planeTolerance = options.planeDistanceTolerance > 0.0f
        ? options.planeDistanceTolerance
        : qMax(0.0001f, plane.meanDistance * 3.0f);

    result.retainedPoints.reserve(points.size());
    result.retainedSourceIndices.reserve(points.size());
    for (int index = 0; index < points.size(); ++index) {
        const Point3D &point = points[index];
        const QVector3D delta(point.x - center.x(), point.y - center.y(), point.z - center.z());
        const float axialDistance = QVector3D::dotProduct(delta, normal);
        const QVector3D planarDelta = delta - axialDistance * normal;
        const float radialDistance = planarDelta.length();

        // 先按投影半径锁定孔内，再由模式决定是否只清理目标表面层；保护带内绝不删除。
        const bool insideProtectedBoundary = radialDistance < protectedInteriorRadius;
        const bool onTargetSurface = std::abs(axialDistance) <= planeTolerance;
        const bool remove = insideProtectedBoundary
            && (options.mode == CircleInteriorCleanupMode::ClearProjection
                || onTargetSurface);
        if (remove) {
            result.removedPoints.push_back(point);
        } else {
            result.retainedPoints.push_back(point);
            result.retainedSourceIndices.push_back(index);
        }
    }
    result.usedProtectionWidth = protectionWidth;
    result.ok = true;
    result.summary = QStringLiteral("圆孔净化预览：保留 %1 点，待删除 %2 点，边缘保护带 %3 mm")
        .arg(result.retainedPoints.size())
        .arg(result.removedPoints.size())
        .arg(protectionWidth, 0, 'f', 4);
    return result;
}

SimilarCircleSearchResult findSimilarCirclesOnPlane(
    const QVector<Point3D> &points,
    const PlaneModel &plane,
    const CircleDetectionResult &reference,
    const SimilarCircleSearchOptions &options) {
    SimilarCircleSearchResult result;
    if (!reference.ok || reference.radius <= 1.0e-6f) {
        result.error = QStringLiteral("参考圆无效，请先框选并识别一个圆");
        return result;
    }
    if (points.size() < 6) {
        result.error = QStringLiteral("目标平面点数不足，无法搜索相似圆");
        return result;
    }

    // 在确认平面上建立二维投影，把三维相似圆搜索转换为可加速的网格投票问题。
    QVector3D normal(plane.a, plane.b, plane.c);
    const float normalLength = normal.length();
    if (normalLength <= 1.0e-8f) {
        result.error = QStringLiteral("目标平面法向无效");
        return result;
    }
    normal /= normalLength;
    const QVector3D helper = std::abs(normal.z()) < 0.9f
        ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
    const QVector3D u = QVector3D::crossProduct(helper, normal).normalized();
    const QVector3D v = QVector3D::crossProduct(normal, u).normalized();
    const QVector3D origin = -(plane.d / normalLength) * normal;

    struct ProjectedPoint { double x; double y; int sourceIndex; };
    QVector<ProjectedPoint> projected;
    projected.reserve(points.size());
    double minX = std::numeric_limits<double>::max();
    double minY = minX;
    double maxX = -minX;
    double maxY = -minX;
    for (int i = 0; i < points.size(); ++i) {
        const Point3D &point = points[i];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            continue;
        const QVector3D delta = QVector3D(point.x, point.y, point.z) - origin;
        const double x = QVector3D::dotProduct(delta, u);
        const double y = QVector3D::dotProduct(delta, v);
        projected.push_back({x, y, i});
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    if (projected.size() < 6) {
        result.error = QStringLiteral("目标平面没有足够的有效点");
        return result;
    }

    const double width = std::max(maxX - minX, 1.0e-6);
    const double height = std::max(maxY - minY, 1.0e-6);
    const double areaSpacing = std::sqrt((width * height) / projected.size());
    double cellSize = std::max({areaSpacing * 0.75,
                                double(reference.radius) / 120.0,
                                1.0e-5});
    const double longestSide = std::max(width, height);
    if (longestSide / cellSize > 1600.0)
        cellSize = longestSide / 1600.0;

    struct GridKey {
        int x;
        int y;
        bool operator==(const GridKey &other) const { return x == other.x && y == other.y; }
    };
    struct GridKeyHash {
        std::size_t operator()(const GridKey &key) const noexcept {
            const quint64 packed = (quint64(quint32(key.x)) << 32) | quint32(key.y);
            return std::size_t(packed ^ (packed >> 29) ^ (packed << 17));
        }
    };

    // 先将投影点栅格化并提取缺邻居的边界格，减少 RANSAC 候选的数量。
    std::unordered_map<GridKey, int, GridKeyHash> occupied;
    occupied.reserve(std::size_t(projected.size()));
    for (int i = 0; i < projected.size(); ++i) {
        const int gx = int(std::floor((projected[i].x - minX) / cellSize));
        const int gy = int(std::floor((projected[i].y - minY) / cellSize));
        occupied.emplace(GridKey{gx, gy}, i);
    }

    QVector<ProjectedPoint> boundary;
    boundary.reserve(int(occupied.size() / 3));
    static constexpr int neighborOffsets[8][2] = {
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
        {1, 0}, {-1, 1}, {0, 1}, {1, 1}
    };
    for (const auto &entry : occupied) {
        bool isBoundary = false;
        for (const auto &offset : neighborOffsets) {
            if (occupied.find(GridKey{entry.first.x + offset[0],
                                      entry.first.y + offset[1]}) == occupied.end()) {
                isBoundary = true;
                break;
            }
        }
        if (isBoundary) boundary.push_back(projected[entry.second]);
    }
    if (boundary.size() < 6) {
        result.error = QStringLiteral("平面投影中没有足够的圆周边界点");
        return result;
    }

    const int boundaryLimit = qMax(500, options.maximumBoundarySamples);
    QVector<ProjectedPoint> votingPoints;
    if (boundary.size() <= boundaryLimit) {
        votingPoints = boundary;
    } else {
        votingPoints.reserve(boundaryLimit);
        const double step = double(boundary.size()) / boundaryLimit;
        for (int i = 0; i < boundaryLimit; ++i)
            votingPoints.push_back(boundary[qMin(boundary.size() - 1, int(i * step))]);
    }

    struct VoteKey {
        int x;
        int y;
        int radiusLevel;
        bool operator==(const VoteKey &other) const {
            return x == other.x && y == other.y && radiusLevel == other.radiusLevel;
        }
    };
    struct VoteKeyHash {
        std::size_t operator()(const VoteKey &key) const noexcept {
            quint64 value = quint64(quint32(key.x)) * UINT64_C(0x9e3779b185ebca87);
            value ^= quint64(quint32(key.y)) * UINT64_C(0xc2b2ae3d27d4eb4f);
            value ^= quint64(quint32(key.radiusLevel + 17)) * UINT64_C(0x165667b19e3779f9);
            return std::size_t(value ^ (value >> 32));
        }
    };

    const double radiusTolerance = qBound(0.005, double(options.radiusToleranceRatio), 0.50);
    const QVector<double> radiusLevels = {
        double(reference.radius) * (1.0 - radiusTolerance),
        double(reference.radius),
        double(reference.radius) * (1.0 + radiusTolerance)
    };
    const double centerCell = std::max(cellSize * 2.0, double(reference.radius) / 40.0);
    const int angleSteps = 32;
    const double pi2 = 2.0 * std::acos(-1.0);
    std::unordered_map<VoteKey, int, VoteKeyHash> votes;
    votes.reserve(std::size_t(votingPoints.size()) * 8);
    // 每个边界点反推若干圆心/半径假设并累积票数，票数最高的提案优先细化。
    for (const ProjectedPoint &point : votingPoints) {
        for (int level = 0; level < radiusLevels.size(); ++level) {
            const double radius = radiusLevels[level];
            for (int angleIndex = 0; angleIndex < angleSteps; ++angleIndex) {
                const double angle = pi2 * angleIndex / angleSteps;
                const double centerX = point.x - radius * std::cos(angle);
                const double centerY = point.y - radius * std::sin(angle);
                if (centerX < minX - radius || centerX > maxX + radius
                    || centerY < minY - radius || centerY > maxY + radius)
                    continue;
                const int gx = int(std::llround((centerX - minX) / centerCell));
                const int gy = int(std::llround((centerY - minY) / centerCell));
                ++votes[VoteKey{gx, gy, level}];
            }
        }
    }

    struct Proposal { double x; double y; double radius; int votes; };
    QVector<Proposal> proposals;
    proposals.reserve(int(votes.size()));
    for (const auto &entry : votes) {
        if (entry.second < 4) continue;
        proposals.push_back({minX + entry.first.x * centerCell,
                             minY + entry.first.y * centerCell,
                             radiusLevels[entry.first.radiusLevel],
                             entry.second});
    }
    std::sort(proposals.begin(), proposals.end(), [](const Proposal &left, const Proposal &right) {
        return left.votes > right.votes;
    });
    const int proposalLimit = qMin(proposals.size(), qMax(24, options.maximumCandidates * 12));

    CircleCandidate referenceCandidate;
    referenceCandidate.circle = reference;
    referenceCandidate.circle.angularBinCount = reference.angularBinCount > 0
        ? reference.angularBinCount : qMax(12, options.angularBins);
    referenceCandidate.angularCoverage = referenceCandidate.circle.angularBinCount > 0
        ? float(reference.occupiedBins) / referenceCandidate.circle.angularBinCount : 1.0f;
    referenceCandidate.densitySimilarity = 1.0f;
    referenceCandidate.similarity = 1.0f;
    referenceCandidate.isReference = true;
    result.candidates.push_back(referenceCandidate);

    const int angularBins = qBound(12, options.angularBins, 180);
    const float minCoverage = qBound(0.10f, options.minimumAngularCoverage, 0.95f);
    const double detectionTolerance = options.radialTolerance > 0.0f
        ? options.radialTolerance
        : std::max({cellSize * 2.5, double(reference.radius) * 0.025, 1.0e-4});
    const double annulusHalfWidth = std::max({detectionTolerance * 2.0,
                                              double(reference.radius) * radiusTolerance * 1.5,
                                              centerCell * 1.5});
    const double referenceDensity = double(qMax(1, reference.inlierIndices.size()))
                                    / qMax(1, reference.occupiedBins);

    for (int proposalIndex = 0; proposalIndex < proposalLimit; ++proposalIndex) {
        const Proposal &proposal = proposals[proposalIndex];
        QVector<Point3D> region;
        QVector<int> regionSourceIndices;
        for (const ProjectedPoint &point : projected) {
            const double radialDistance = std::hypot(point.x - proposal.x,
                                                      point.y - proposal.y);
            if (std::abs(radialDistance - proposal.radius) <= annulusHalfWidth) {
                region.push_back(points[point.sourceIndex]);
                regionSourceIndices.push_back(point.sourceIndex);
            }
        }
        if (region.size() < 8) continue;

        CircleDetectionOptions detectionOptions;
        detectionOptions.radialTolerance = float(detectionTolerance);
        detectionOptions.iterations = 500;
        detectionOptions.angularBins = angularBins;
        detectionOptions.minOccupiedBins = qMax(6, int(std::ceil(minCoverage * angularBins)));
        detectionOptions.minInliers = qBound(6,
            qMax(8, reference.inlierIndices.size() / 5), region.size());
        detectionOptions.randomSeed = options.maximumCandidates * 131u
                                      + unsigned(proposalIndex) * 17u + 20260810u;
        CircleDetectionResult detected = detectCircleOnPlane(region, plane, detectionOptions);
        if (!detected.ok) continue;

        QVector<int> globalInliers;
        globalInliers.reserve(detected.inlierIndices.size());
        for (int localIndex : detected.inlierIndices) {
            if (localIndex >= 0 && localIndex < regionSourceIndices.size())
                globalInliers.push_back(regionSourceIndices[localIndex]);
        }
        detected.inlierIndices = globalInliers;

        const double refinedCenterX = QVector3D::dotProduct(
            QVector3D(detected.center.x, detected.center.y, detected.center.z) - origin, u);
        const double refinedCenterY = QVector3D::dotProduct(
            QVector3D(detected.center.x, detected.center.y, detected.center.z) - origin, v);
        if (std::hypot(refinedCenterX - proposal.x, refinedCenterY - proposal.y)
            > reference.radius * 0.35)
            continue;

        const float radiusDifference = std::abs(detected.radius - reference.radius)
                                       / reference.radius;
        if (radiusDifference > options.radiusToleranceRatio) continue;
        const float coverage = detected.angularBinCount > 0
            ? float(detected.occupiedBins) / detected.angularBinCount : 0.0f;
        if (coverage < minCoverage) continue;
        const float candidateDensity = float(detected.inlierIndices.size())
                                       / qMax(1, detected.occupiedBins);
        const float densitySimilarity = referenceDensity > 0.0
            ? float(std::min(candidateDensity / referenceDensity,
                             referenceDensity / qMax(1.0e-6f, candidateDensity))) : 1.0f;
        const float radiusScore = qBound(0.0f,
            1.0f - radiusDifference / qMax(0.005f, options.radiusToleranceRatio), 1.0f);
        const float rmsScore = float(std::exp(-detected.rmsError
                                              / qMax(1.0e-6, detectionTolerance)));
        const float similarity = 0.35f * radiusScore + 0.25f * rmsScore
                                 + 0.25f * coverage + 0.15f * densitySimilarity;
        if (similarity < options.minimumSimilarity) continue;

        CircleCandidate candidate;
        candidate.circle = detected;
        candidate.similarity = similarity;
        candidate.angularCoverage = coverage;
        candidate.densitySimilarity = densitySimilarity;
        const QVector3D candidateCenter(detected.center.x, detected.center.y, detected.center.z);
        const QVector3D referenceCenter(reference.center.x, reference.center.y, reference.center.z);
        candidate.isReference = (candidateCenter - referenceCenter).length()
                                < reference.radius * 0.35f;

        int duplicateIndex = -1;
        for (int i = 0; i < result.candidates.size(); ++i) {
            const auto &existing = result.candidates[i].circle.center;
            const QVector3D existingCenter(existing.x, existing.y, existing.z);
            if ((candidateCenter - existingCenter).length()
                < qMin(candidate.circle.radius, result.candidates[i].circle.radius) * 0.60f) {
                duplicateIndex = i;
                break;
            }
        }
        if (duplicateIndex < 0) {
            result.candidates.push_back(candidate);
        } else if (!result.candidates[duplicateIndex].isReference
                   && candidate.similarity > result.candidates[duplicateIndex].similarity) {
            result.candidates[duplicateIndex] = candidate;
        }
    }

    std::sort(result.candidates.begin(), result.candidates.end(),
              [](const CircleCandidate &left, const CircleCandidate &right) {
        if (left.isReference != right.isReference) return left.isReference;
        return left.similarity > right.similarity;
    });
    if (result.candidates.size() > options.maximumCandidates)
        result.candidates.resize(options.maximumCandidates);
    result.ok = !result.candidates.isEmpty();
    result.summary = QStringLiteral("相似圆搜索完成：参考圆 1 个，匹配候选 %1 个")
        .arg(qMax(0, result.candidates.size() - 1));
    return result;
}

PlaneSegmentationResult segmentPlanes(const QVector<Point3D> &points,
                                      const PlaneSegmentationOptions &options) {
    PlaneSegmentationResult result;
    if (points.size() < 3) { result.error = QStringLiteral("点数不足，至少需要 3 个点"); return result; }
    const int denominator = qMax(1, options.sampleDenominator);
    const QVector<Point3D> sampled = proportionalDownsample(points, denominator);
    QVector<int> active;
    active.reserve(sampled.size());
    const bool hasEdgeMask = options.edgeMask.size() == points.size();
    for (int i = 0; i < sampled.size(); ++i) {
        const int original = i * denominator;
        if (usablePoint(sampled[i]) && (!hasEdgeMask || original >= points.size() || !options.edgeMask[original]))
            active.push_back(i);
    }
    if (active.size() < 3) {
        // A percentile mask can legitimately mark nearly every point on a
        // small or highly curved patch.  Fall back to all samples rather than
        // failing the complete pipeline.
        active.clear();
        for (int i = 0; i < sampled.size(); ++i) if (usablePoint(sampled[i])) active.push_back(i);
    }
    // 逐轮 RANSAC 选取三点拟合平面；确认一张平面后从活动集合剔除其内点，
    // 再继续寻找下一张平面，直到达到数量上限或剩余点不足。
    std::mt19937 rng(options.randomSeed);
    const float threshold = qMax(1.0e-6f, options.distanceThreshold);
    const int minInliers = qMax(3, options.minInliers / denominator);
    for (int planeIndex = 0; planeIndex < qMax(1, options.maxPlanes); ++planeIndex) {
        if (active.size() < minInliers) break;
        int bestCount = 0; float ba = 0, bb = 0, bc = 1, bd = 0;
        QVector<int> bestInliers;
        std::uniform_int_distribution<int> pick(0, active.size() - 1);
        for (int iteration = 0; iteration < qMax(50, options.iterations); ++iteration) {
            const int ia = pick(rng), ib = pick(rng), ic = pick(rng);
            if (ia == ib || ia == ic || ib == ic) continue;
            const Point3D &p1 = sampled[active[ia]];
            const Point3D &p2 = sampled[active[ib]];
            const Point3D &p3 = sampled[active[ic]];
            const float ux = p2.x-p1.x, uy = p2.y-p1.y, uz = p2.z-p1.z;
            const float vx = p3.x-p1.x, vy = p3.y-p1.y, vz = p3.z-p1.z;
            float a = uy*vz-uz*vy, b = uz*vx-ux*vz, c = ux*vy-uy*vx;
            const float norm = std::sqrt(a*a+b*b+c*c);
            if (norm < 1.0e-7f) continue;
            a /= norm; b /= norm; c /= norm;
            if (options.preferHorizontal) {
                const float minVerticalNormal = std::cos(qDegreesToRadians(
                    qBound(0.1f, options.maxTiltDegrees, 89.0f)));
                if (std::abs(c) < minVerticalNormal) continue;
            }
            const float d = -(a*p1.x+b*p1.y+c*p1.z);
            QVector<int> inliers;
            inliers.reserve(active.size()/3);
            for (int index : active) {
                const Point3D &p = sampled[index];
                if (std::abs(a*p.x+b*p.y+c*p.z+d) <= threshold) inliers.push_back(index);
            }
            if (inliers.size() > bestCount) { bestCount = inliers.size(); bestInliers = inliers; ba=a; bb=b; bc=c; bd=d; }
        }
        if (bestCount < minInliers) break;
        PlaneModel model; model.a=ba; model.b=bb; model.c=bc; model.d=bd;
        QVector<quint8> remove(sampled.size(), 0);
        for (int index : bestInliers) {
            remove[index] = 1;
        }
        result.planes.push_back(model);
        QVector<int> remaining; remaining.reserve(active.size()-bestCount);
        for (int index : active) if (!remove[index]) remaining.push_back(index);
        active.swap(remaining);
    }
    result.labels.fill(-1, points.size());
    for (int i = 0; i < points.size(); ++i) {
        float best = threshold; int label = -1;
        for (int p = 0; p < result.planes.size(); ++p) {
            const PlaneModel &model = result.planes[p];
            const float distance = std::abs(model.a*points[i].x + model.b*points[i].y + model.c*points[i].z + model.d);
            if (distance <= best) { best = distance; label = p; }
        }
        result.labels[i] = label;
    }
    // RANSAC uses the sampled cache only to estimate models. Statistics are
    // computed after projecting every model back onto the complete canvas
    // cache, so reported counts and distances match the visible data.
    QVector<double> distanceSums(result.planes.size(), 0.0);
    for (PlaneModel &plane : result.planes) {
        plane.inlierCount = 0;
        plane.meanDistance = 0.0f;
        plane.maxDistance = 0.0f;
    }
    for (int i = 0; i < points.size(); ++i) {
        const int label = result.labels[i];
        if (label < 0) continue;
        PlaneModel &plane = result.planes[label];
        const Point3D &point = points[i];
        const float distance = std::abs(plane.a * point.x + plane.b * point.y
                                        + plane.c * point.z + plane.d);
        ++plane.inlierCount;
        distanceSums[label] += distance;
        plane.maxDistance = qMax(plane.maxDistance, distance);
    }
    for (int i = 0; i < result.planes.size(); ++i) {
        PlaneModel &plane = result.planes[i];
        if (plane.inlierCount > 0)
            plane.meanDistance = float(distanceSums[i] / double(plane.inlierCount));
    }
    result.ok = !result.planes.isEmpty();
    if (!result.ok) result.error = QStringLiteral("未找到满足条件的平面");
    else result.summary = QStringLiteral("检测到 %1 个平面\n输入点：%2\n采样比例：1/%3\n未分类点：%4")
        .arg(result.planes.size()).arg(points.size()).arg(denominator)
        .arg(std::count(result.labels.cbegin(), result.labels.cend(), -1));
    return result;
}

EdgePipelineResult runEdgeAwarePipeline(const QVector<Point3D> &points,
                                        const EdgePipelineOptions &options) {
    EdgePipelineResult result;
    if (points.size() < 3) { result.error = QStringLiteral("点数不足，至少需要 3 个点"); return result; }

    // 第一层：用局部 PCA 曲率/散乱度计算边缘分数，并按分位数生成初始掩码。
    const GeometryFeatureResult features = extractGeometryFeatures(points, options.feature);
    if (!features.ok) { result.error = features.error; return result; }
    result.edgeScore.resize(points.size());
    QVector<float> scores;
    scores.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const GeometryFeature &f = features.features[i];
        const float score = f.valid ? qBound(0.0f, f.curvature * 3.0f + f.scattering, 1.0f) : 0.0f;
        result.edgeScore[i] = score;
        if (f.valid) scores.push_back(score);
    }
    std::sort(scores.begin(), scores.end());
    const float percentile = qBound(0.5f, options.edgePercentile, 0.999f);
    const int cut = scores.isEmpty() ? 0 : qBound(0, int(std::floor(percentile * (scores.size() - 1))), scores.size() - 1);
    const float threshold = options.edgeCurvatureThreshold > 0.0f
        ? options.edgeCurvatureThreshold : (scores.isEmpty() ? 1.0f : scores[cut]);
    result.edgeMask.resize(points.size());
    int edgeCount = 0;
    for (int i = 0; i < points.size(); ++i) {
        const bool edge = features.features[i].valid && result.edgeScore[i] >= threshold;
        result.edgeMask[i] = edge ? 1 : 0;
        if (edge) ++edgeCount;
    }

    // Suppress isolated edge points using a spatial hash.  The previous
    // all-pairs scan made edge refinement O(N²) and its fixed 2 mm radius was
    // unusable when the cloud unit/spacing changed.
    if (!points.isEmpty()) {
        const float radius = options.edgeNeighborRadius > 0.0f
            ? options.edgeNeighborRadius
            : (options.feature.searchRadius > 0.0f ? options.feature.searchRadius * 1.5f : 2.0f);
        const float cell = qMax(radius, 1.0e-4f);
        const int neighborMin = qMax(1, options.edgeMinNeighbors);
        using Cell = std::array<int, 3>;
        struct CellHash {
            std::size_t operator()(const Cell &c) const noexcept {
                const std::size_t h1 = std::hash<int>{}(c[0]);
                const std::size_t h2 = std::hash<int>{}(c[1]);
                const std::size_t h3 = std::hash<int>{}(c[2]);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };
        std::unordered_map<Cell, QVector<int>, CellHash> grid;
        grid.reserve(std::size_t(std::count(result.edgeMask.cbegin(), result.edgeMask.cend(), quint8(1))));
        auto keyFor = [cell](const Point3D &p) {
            return Cell{int(std::floor(p.x / cell)), int(std::floor(p.y / cell)), int(std::floor(p.z / cell))};
        };
        for (int i = 0; i < points.size(); ++i)
            if (result.edgeMask[i]) grid[keyFor(points[i])].push_back(i);
        const float radius2 = radius * radius;
        QVector<quint8> refined = result.edgeMask;
        for (int i = 0; i < points.size(); ++i) {
            if (!result.edgeMask[i]) continue;
            const Cell key = keyFor(points[i]);
            int nearby = 0;
            for (int dx = -1; dx <= 1 && nearby < neighborMin; ++dx)
                for (int dy = -1; dy <= 1 && nearby < neighborMin; ++dy)
                    for (int dz = -1; dz <= 1 && nearby < neighborMin; ++dz) {
                        const Cell neighbor{key[0] + dx, key[1] + dy, key[2] + dz};
                        const auto it = grid.find(neighbor);
                        if (it == grid.end()) continue;
                        for (int j : it->second) {
                            if (i == j) continue;
                            const auto &a = points[i]; const auto &b = points[j];
                            const float ddx=a.x-b.x, ddy=a.y-b.y, ddz=a.z-b.z;
                            if (ddx*ddx + ddy*ddy + ddz*ddz <= radius2 && ++nearby >= neighborMin) break;
                        }
                    }
            if (nearby < neighborMin) refined[i] = 0;
        }
        result.edgeMask.swap(refined);
    }
    edgeCount = std::count(result.edgeMask.cbegin(), result.edgeMask.cend(), quint8(1));

    // 第二层：仅对非边缘点去噪，边缘点原样保留，避免尖锐结构被误删。
    QVector<Point3D> nonEdges, edges;
    nonEdges.reserve(points.size() - edgeCount); edges.reserve(edgeCount);
    for (int i = 0; i < points.size(); ++i)
        (result.edgeMask[i] ? edges : nonEdges).push_back(points[i]);
    const NoiseResult cleaned = removeNoise(nonEdges, options.denoise);
    // A very small non-edge population cannot satisfy statistical-neighbour
    // requirements; retain it unchanged so the pipeline remains usable.
    result.filteredPoints = cleaned.ok ? cleaned.points : nonEdges;
    result.filteredPoints += edges;

    // 第三层：拟合平面时屏蔽边缘点，但最终把所有保留点重新分类到平面模型。
    PlaneSegmentationOptions planeOptions = options.planes;
    planeOptions.edgeMask.resize(result.filteredPoints.size());
    const int nonEdgeCount = result.filteredPoints.size() - edges.size();
    for (int i = 0; i < nonEdgeCount; ++i) planeOptions.edgeMask[i] = 0;
    for (int i = nonEdgeCount; i < result.filteredPoints.size(); ++i) planeOptions.edgeMask[i] = 1;
    result.planeResult = segmentPlanes(result.filteredPoints, planeOptions);
    if (!result.planeResult.ok && !planeOptions.edgeMask.isEmpty()) {
        planeOptions.edgeMask.clear();
        result.planeResult = segmentPlanes(result.filteredPoints, planeOptions);
    }

    // 第四层：只导出边缘点的几何描述，同时保持与原输入点一一对齐。
    result.edgeFeatures.resize(points.size());
    for (int i = 0; i < points.size(); ++i)
        if (result.edgeMask[i]) result.edgeFeatures[i] = features.features[i];
    result.ok = result.planeResult.ok;
    if (!result.ok) { result.error = result.planeResult.error; return result; }
    result.summary = QStringLiteral("四层边缘流水线完成：输入 %1 点，边缘 %2 点，去噪后 %3 点，平面 %4 个")
        .arg(points.size()).arg(edgeCount).arg(result.filteredPoints.size())
        .arg(result.planeResult.planes.size());
    return result;
}

namespace {

QVector<quint8> dilateMask(const QVector<quint8> &source, int width, int height,
                           int radius) {
    if (radius <= 0) return source;
    QVector<quint8> output(width * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool occupied = false;
            for (int dy = -radius; dy <= radius && !occupied; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= height) continue;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    if (nx >= 0 && nx < width && source[ny * width + nx]) {
                        occupied = true;
                        break;
                    }
                }
            }
            output[y * width + x] = occupied ? 1 : 0;
        }
    }
    return output;
}

QVector<quint8> erodeMask(const QVector<quint8> &source, int width, int height,
                          int radius) {
    if (radius <= 0) return source;
    QVector<quint8> output(width * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool occupied = true;
            for (int dy = -radius; dy <= radius && occupied; ++dy) {
                const int ny = y + dy;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height
                        || !source[ny * width + nx]) {
                        occupied = false;
                        break;
                    }
                }
            }
            output[y * width + x] = occupied ? 1 : 0;
        }
    }
    return output;
}

struct GridVertex {
    int x2 = 0;
    int y2 = 0;
    bool operator==(const GridVertex &other) const {
        return x2 == other.x2 && y2 == other.y2;
    }
};

struct GridSegment { GridVertex a; GridVertex b; };

quint64 gridVertexKey(const GridVertex &vertex) {
    return (quint64(quint32(vertex.x2)) << 32) | quint32(vertex.y2);
}

double signedArea(const QVector<QVector2D> &polygon) {
    double area = 0.0;
    for (int i = 0; i + 1 < polygon.size(); ++i)
        area += double(polygon[i].x()) * polygon[i + 1].y()
            - double(polygon[i + 1].x()) * polygon[i].y();
    return area * 0.5;
}

bool pointInPolygon(const QVector2D &point, const QVector<QVector2D> &polygon) {
    bool inside = false;
    for (int i = 0, j = polygon.size() - 2; i + 1 < polygon.size(); j = i++) {
        const QVector2D &a = polygon[i];
        const QVector2D &b = polygon[j];
        const bool crosses = (a.y() > point.y()) != (b.y() > point.y());
        if (crosses && point.x() < (b.x() - a.x()) * (point.y() - a.y())
                / (b.y() - a.y()) + a.x())
            inside = !inside;
    }
    return inside;
}

QVector<QVector<QVector2D>> marchingSquaresContours(const QVector<quint8> &mask,
                                                     int width, int height,
                                                     float gridSize,
                                                     float minimumU,
                                                     float minimumV) {
    QVector<GridSegment> segments;
    segments.reserve(width * 2 + height * 2);
    const auto append = [&segments](GridVertex a, GridVertex b) {
        segments.push_back({a, b});
    };
    for (int y = 0; y + 1 < height; ++y) {
        for (int x = 0; x + 1 < width; ++x) {
            const int code = (mask[y * width + x] ? 1 : 0)
                | (mask[y * width + x + 1] ? 2 : 0)
                | (mask[(y + 1) * width + x + 1] ? 4 : 0)
                | (mask[(y + 1) * width + x] ? 8 : 0);
            if (code == 0 || code == 15) continue;
            const GridVertex bottom{2 * x + 1, 2 * y};
            const GridVertex right{2 * x + 2, 2 * y + 1};
            const GridVertex top{2 * x + 1, 2 * y + 2};
            const GridVertex left{2 * x, 2 * y + 1};
            switch (code) {
            case 1: append(left, bottom); break;
            case 2: append(bottom, right); break;
            case 3: append(left, right); break;
            case 4: append(right, top); break;
            case 5: append(left, top); append(bottom, right); break;
            case 6: append(bottom, top); break;
            case 7: append(left, top); break;
            case 8: append(top, left); break;
            case 9: append(top, bottom); break;
            case 10: append(bottom, left); append(right, top); break;
            case 11: append(top, right); break;
            case 12: append(right, left); break;
            case 13: append(right, bottom); break;
            case 14: append(bottom, left); break;
            default: break;
            }
        }
    }

    std::unordered_map<quint64, QVector<int>> adjacency;
    adjacency.reserve(std::size_t(segments.size() * 2));
    for (int i = 0; i < segments.size(); ++i) {
        adjacency[gridVertexKey(segments[i].a)].push_back(i);
        adjacency[gridVertexKey(segments[i].b)].push_back(i);
    }
    QVector<quint8> used(segments.size(), 0);
    QVector<QVector<QVector2D>> contours;
    const auto toUv = [gridSize, minimumU, minimumV](const GridVertex &vertex) {
        return QVector2D(minimumU + 0.5f * float(vertex.x2) * gridSize,
                         minimumV + 0.5f * float(vertex.y2) * gridSize);
    };
    for (int start = 0; start < segments.size(); ++start) {
        if (used[start]) continue;
        QVector<GridVertex> chain{segments[start].a, segments[start].b};
        used[start] = 1;
        GridVertex current = segments[start].b;
        while (!(current == chain.first())) {
            const auto found = adjacency.find(gridVertexKey(current));
            if (found == adjacency.end()) break;
            int nextSegment = -1;
            for (int candidate : found->second)
                if (!used[candidate]) { nextSegment = candidate; break; }
            if (nextSegment < 0) break;
            used[nextSegment] = 1;
            const GridSegment &segment = segments[nextSegment];
            current = segment.a == current ? segment.b : segment.a;
            chain.push_back(current);
            if (chain.size() > segments.size() + 1) break;
        }
        if (chain.size() < 4 || !(chain.first() == chain.last())) continue;
        QVector<QVector2D> contour;
        contour.reserve(chain.size());
        for (const GridVertex &vertex : chain) contour.push_back(toUv(vertex));
        contours.push_back(std::move(contour));
    }
    return contours;
}

} // namespace

WorldCloudMergeResult mergePlyCloudsInWorld(const QVector<WorldCloudInput> &inputs,
                                             const IcpOptions &icp) {
    return mergePlyCloudsInWorldImpl(inputs, icp);
}

ThreePointPlaneResult extractPlaneFromThreePoints(const QVector<Point3D> &points,
                                                  const QVector<int> &seedIndices,
                                                  const ThreePointPlaneOptions &options) {
    ThreePointPlaneResult result;
    if (seedIndices.size() != 3 || points.size() < 3) {
        result.error = QStringLiteral("需要三个有效采样点");
        return result;
    }
    const auto finitePoint = [](const Point3D &point) {
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    };
    for (int seed : seedIndices) {
        if (seed < 0 || seed >= points.size() || !finitePoint(points[seed])) {
            result.error = QStringLiteral("采样点索引无效");
            return result;
        }
        result.controlPoints.push_back(points[seed]);
    }

    const auto vectorFor = [&points](int index) {
        const Point3D &p = points[index];
        return QVector3D(p.x, p.y, p.z);
    };
    const QVector3D p1 = vectorFor(seedIndices[0]);
    const QVector3D p2 = vectorFor(seedIndices[1]);
    const QVector3D p3 = vectorFor(seedIndices[2]);
    const float longestEdge = std::max({(p2 - p1).length(), (p3 - p1).length(),
                                        (p3 - p2).length()});
    if (longestEdge <= 1.0e-7f) {
        result.error = QStringLiteral("三个采样点距离过近");
        return result;
    }
    QVector3D initialNormal = QVector3D::crossProduct(p2 - p1, p3 - p1);
    if (initialNormal.lengthSquared() <= longestEdge * longestEdge * longestEdge
            * longestEdge * 1.0e-6f) {
        result.error = QStringLiteral("三个采样点共线或距离过近");
        return result;
    }
    initialNormal.normalize();
    if (initialNormal.z() < 0.0f) initialNormal = -initialNormal;
    const float minimumNormalZ = options.useZAxisResidual
        ? std::cos(qDegreesToRadians(qBound(0.0f, options.maxNormalTiltDegrees, 89.0f)))
        : 0.0f;
    if (options.useZAxisResidual && initialNormal.z() < minimumNormalZ) {
        result.error = QStringLiteral("所选平面过于陡峭，不符合 2.5D 高度面约束");
        return result;
    }
    const float initialD = -QVector3D::dotProduct(initialNormal, p1);
    const float initialTolerance = qMax(1.0e-6f, options.initialTolerance);
    const float surfaceTolerance = qMax(1.0e-6f, options.surfaceTolerance);
    const auto residual = [&options](const QVector3D &normal, float d,
                                     const Point3D &point) {
        const float equation = normal.x() * point.x + normal.y() * point.y
            + normal.z() * point.z + d;
        return std::abs(equation) / (options.useZAxisResidual
            ? qMax(std::abs(normal.z()), 1.0e-6f) : 1.0f);
    };

    for (int i = 0; i < points.size(); ++i) {
        const Point3D &p = points[i];
        if (!finitePoint(p)) continue;
        const float distance = residual(initialNormal, initialD, p);
        if (distance <= initialTolerance) result.candidateIndices.push_back(i);
    }
    const int requiredInliers = qMax(3, options.minInliers);
    if (result.candidateIndices.size() < requiredInliers) {
        result.error = QStringLiteral("初始平面附近候选点不足：%1 / %2")
                           .arg(result.candidateIndices.size()).arg(requiredInliers);
        return result;
    }

    QVector<int> modelCandidates;
    const int maximumModelPoints = 20000;
    const int modelStep = qMax(1, int((result.candidateIndices.size()
                                      + maximumModelPoints - 1) / maximumModelPoints));
    modelCandidates.reserve(qMin(maximumModelPoints, int(result.candidateIndices.size())));
    for (int i = 0; i < result.candidateIndices.size(); i += modelStep)
        modelCandidates.push_back(result.candidateIndices[i]);
    QVector<int> bestModelInliers;
    QVector3D bestNormal = initialNormal;
    float bestD = initialD;
    // Treat the user-defined plane as the baseline model. Random RANSAC
    // samples may replace it only when they explain more nearby points.
    for (int index : modelCandidates) {
        const Point3D &p = points[index];
        if (residual(initialNormal, initialD, p) <= surfaceTolerance) {
            bestModelInliers.push_back(index);
        }
    }
    std::mt19937 rng(options.randomSeed);
    std::uniform_int_distribution<int> pick(0, modelCandidates.size() - 1);
    for (int iteration = 0; iteration < qMax(1, options.ransacIterations); ++iteration) {
        const int ia = pick(rng), ib = pick(rng), ic = pick(rng);
        if (ia == ib || ia == ic || ib == ic) continue;
        const QVector3D a = vectorFor(modelCandidates[ia]);
        const QVector3D b = vectorFor(modelCandidates[ib]);
        const QVector3D c = vectorFor(modelCandidates[ic]);
        QVector3D normal = QVector3D::crossProduct(b - a, c - a);
        if (normal.lengthSquared() <= 1.0e-14f) continue;
        normal.normalize();
        if (QVector3D::dotProduct(normal, initialNormal) < 0.0f) normal = -normal;
        if (options.useZAxisResidual && std::abs(normal.z()) < minimumNormalZ) continue;
        const float d = -QVector3D::dotProduct(normal, a);
        bool containsSeeds = true;
        for (int seed : seedIndices) {
            const Point3D &p = points[seed];
            if (residual(normal, d, p) > surfaceTolerance) {
                containsSeeds = false;
                break;
            }
        }
        if (!containsSeeds) continue;
        QVector<int> inliers;
        inliers.reserve(modelCandidates.size());
        for (int index : modelCandidates) {
            const Point3D &p = points[index];
            if (residual(normal, d, p) <= surfaceTolerance) {
                inliers.push_back(index);
            }
        }
        if (inliers.size() > bestModelInliers.size()) {
            bestModelInliers = std::move(inliers);
            bestNormal = normal;
            bestD = d;
        }
    }
    QVector<int> bestInliers;
    bestInliers.reserve(result.candidateIndices.size());
    for (int index : result.candidateIndices) {
        const Point3D &p = points[index];
        if (residual(bestNormal, bestD, p) <= surfaceTolerance) {
            bestInliers.push_back(index);
        }
    }
    if (bestInliers.size() < requiredInliers) {
        result.error = QStringLiteral("RANSAC 平面内点不足：%1 / %2")
                           .arg(bestInliers.size()).arg(requiredInliers);
        return result;
    }

    QVector3D finalNormal = bestNormal;
    QVector3D origin;
    float finalD = bestD;
    double finalEigenvalues[3] = {};
    const auto fitPcaPlane = [&](const QVector<int> &inliers,
                                 const QVector3D &referenceNormal) {
        double mean[3] = {};
        for (int index : inliers) {
            const Point3D &p = points[index];
            mean[0] += p.x; mean[1] += p.y; mean[2] += p.z;
        }
        for (double &value : mean) value /= inliers.size();
        double covariance[3][3] = {};
        for (int index : inliers) {
            const Point3D &p = points[index];
            const double delta[3] = {p.x - mean[0], p.y - mean[1], p.z - mean[2]};
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                    covariance[row][column] += delta[row] * delta[column];
        }
        double eigenvectors[3][3];
        symmetricEigen3(covariance, finalEigenvalues, eigenvectors);
        finalNormal = {float(eigenvectors[0][2]), float(eigenvectors[1][2]),
                       float(eigenvectors[2][2])};
        if (finalNormal.lengthSquared() <= 1.0e-14f) return false;
        finalNormal.normalize();
        if (QVector3D::dotProduct(finalNormal, referenceNormal) < 0.0f)
            finalNormal = -finalNormal;
        if (finalNormal.z() < 0.0f) finalNormal = -finalNormal;
        if (options.useZAxisResidual && finalNormal.z() < minimumNormalZ) return false;
        origin = {float(mean[0]), float(mean[1]), float(mean[2])};
        finalD = -QVector3D::dotProduct(finalNormal, origin);
        return true;
    };

    QVector<int> refinedInliers = bestInliers;
    const int refinementIterations = qBound(1, options.pcaRefinementIterations, 3);
    for (int iteration = 0; iteration < refinementIterations; ++iteration) {
        if (!fitPcaPlane(refinedInliers, iteration == 0 ? bestNormal : finalNormal)) {
            result.error = QStringLiteral("最小二乘/PCA 平面拟合失败或不符合 2.5D 约束");
            return result;
        }
        ++result.pcaRefinementCount;
        QVector<int> reclassified;
        reclassified.reserve(result.candidateIndices.size());
        for (int index : result.candidateIndices)
            if (residual(finalNormal, finalD, points[index]) <= surfaceTolerance)
                reclassified.push_back(index);
        if (reclassified.size() < requiredInliers) {
            result.error = QStringLiteral("PCA 重分类后平面内点不足：%1 / %2")
                               .arg(reclassified.size()).arg(requiredInliers);
            return result;
        }
        if (reclassified == refinedInliers) break;
        refinedInliers = std::move(reclassified);
    }
    if (!fitPcaPlane(refinedInliers, finalNormal)) {
        result.error = QStringLiteral("最终最小二乘/PCA 平面拟合失败");
        return result;
    }
    result.planarity = float(1.0 - finalEigenvalues[2]
        / qMax(finalEigenvalues[1], 1.0e-20));

    // Preview path: return the fitted plane and candidate inliers without
    // scanning the complete display cache or building connectivity grids.
    // The caller can rerun with deferFinalClassification=false on confirm.
    if (options.deferFinalClassification) {
        result.planeIndices = refinedInliers;
        result.planePoints.reserve(refinedInliers.size());
        double squaredError = 0.0;
        for (int index : refinedInliers) {
            result.planePoints.push_back(points[index]);
            const float distance = residual(finalNormal, finalD, points[index]);
            squaredError += double(distance) * distance;
        }
        result.rmsError = float(std::sqrt(squaredError / qMax(1, refinedInliers.size())));
        result.usedThreshold = surfaceTolerance;
        result.model = {finalNormal.x(), finalNormal.y(), finalNormal.z(), finalD,
                        int(refinedInliers.size()), result.rmsError, surfaceTolerance};
        result.deferred = true;
        result.ok = true;
        return result;
    }

    QVector<int> classified;
    classified.reserve(bestInliers.size());
    for (int i = 0; i < points.size(); ++i) {
        const Point3D &p = points[i];
        if (!finitePoint(p)) continue;
        if (residual(finalNormal, finalD, p) <= surfaceTolerance) {
            classified.push_back(i);
        }
    }

    QVector3D axisU = QVector3D::crossProduct(finalNormal,
        std::abs(finalNormal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
    axisU.normalize();
    const QVector3D axisV = QVector3D::crossProduct(finalNormal, axisU).normalized();
    struct Cell { int x = 0; int y = 0; bool operator==(const Cell &o) const { return x == o.x && y == o.y; } };
    struct CellHash { std::size_t operator()(const Cell &cell) const noexcept {
        return std::hash<int>{}(cell.x) ^ (std::hash<int>{}(cell.y) << 1);
    }};

    QVector<QVector2D> projected; projected.reserve(classified.size());
    for (int index : classified) {
        const QVector3D delta = vectorFor(index) - origin;
        const QVector2D uv(QVector3D::dotProduct(delta, axisU),
                           QVector3D::dotProduct(delta, axisV));
        projected.push_back(uv);
    }
    float projectedMinU = std::numeric_limits<float>::max();
    float projectedMinV = std::numeric_limits<float>::max();
    float projectedMaxU = std::numeric_limits<float>::lowest();
    float projectedMaxV = std::numeric_limits<float>::lowest();
    for (const QVector2D &uv : projected) {
        projectedMinU = qMin(projectedMinU, uv.x());
        projectedMinV = qMin(projectedMinV, uv.y());
        projectedMaxU = qMax(projectedMaxU, uv.x());
        projectedMaxV = qMax(projectedMaxV, uv.y());
    }
    const double projectedArea = double(qMax(projectedMaxU - projectedMinU, 1.0e-6f))
        * double(qMax(projectedMaxV - projectedMinV, 1.0e-6f));
    const float approximateSpacing = qMax(
        float(std::sqrt(projectedArea / qMax(1, projected.size()))), 1.0e-6f);
    const float spacingCellSize = approximateSpacing * 2.0f;
    std::unordered_map<Cell, QVector<int>, CellHash> spacingGrid;
    spacingGrid.reserve(std::size_t(projected.size()));
    for (int local = 0; local < projected.size(); ++local) {
        const QVector2D uv = projected[local];
        spacingGrid[{int(std::floor(uv.x() / spacingCellSize)),
                     int(std::floor(uv.y() / spacingCellSize))}].push_back(local);
    }
    QVector<float> spacingSamples;
    const int spacingProbeCount = qMin(512, int(projected.size()));
    spacingSamples.reserve(spacingProbeCount);
    for (int probe = 0; probe < spacingProbeCount; ++probe) {
        const int probeIndex = int((qint64(probe) * projected.size()) / spacingProbeCount);
        const QVector2D uv = projected[probeIndex];
        const Cell cell{int(std::floor(uv.x() / spacingCellSize)),
                        int(std::floor(uv.y() / spacingCellSize))};
        float nearestSquared = std::numeric_limits<float>::max();
        for (int ring = 1; ring <= 3
             && nearestSquared == std::numeric_limits<float>::max(); ++ring) {
            for (int dy = -ring; dy <= ring; ++dy) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    const auto found = spacingGrid.find({cell.x + dx, cell.y + dy});
                    if (found == spacingGrid.end()) continue;
                    for (int referenceIndex : found->second) {
                        if (referenceIndex == probeIndex) continue;
                        nearestSquared = qMin(nearestSquared,
                            (projected[referenceIndex] - uv).lengthSquared());
                    }
                }
            }
        }
        if (std::isfinite(nearestSquared) && nearestSquared > 1.0e-12f)
            spacingSamples.push_back(std::sqrt(nearestSquared));
    }
    std::sort(spacingSamples.begin(), spacingSamples.end());
    const float estimatedSpacing = spacingSamples.isEmpty() ? approximateSpacing
        : spacingSamples[spacingSamples.size() / 2];
    const float radius = options.connectivityRadius > 0.0f
        ? options.connectivityRadius : qMax(estimatedSpacing * 2.5f, 1.0e-6f);
    const float radiusSquared = radius * radius;
    std::unordered_map<Cell, QVector<int>, CellHash> grid;
    for (int local = 0; local < projected.size(); ++local) {
        const QVector2D uv = projected[local];
        grid[{int(std::floor(uv.x() / radius)), int(std::floor(uv.y() / radius))}].push_back(local);
    }
    QVector<int> component(classified.size(), -1);
    QVector<int> componentSizes;
    int componentCount = 0;
    for (int start = 0; start < classified.size(); ++start) {
        if (component[start] >= 0) continue;
        QVector<int> queue{start}; component[start] = componentCount;
        for (qsizetype head = 0; head < queue.size(); ++head) {
            const int local = queue[head];
            const QVector2D uv = projected[local];
            const Cell cell{int(std::floor(uv.x() / radius)), int(std::floor(uv.y() / radius))};
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const auto found = grid.find({cell.x + dx, cell.y + dy});
                if (found == grid.end()) continue;
                for (int neighbor : found->second) {
                    if (component[neighbor] >= 0) continue;
                    if ((projected[neighbor] - uv).lengthSquared() <= radiusSquared) {
                        component[neighbor] = componentCount;
                        queue.push_back(neighbor);
                    }
                }
            }
        }
        componentSizes.push_back(queue.size());
        ++componentCount;
    }
    result.connectedComponentCount = componentCount;

    int selectedComponent = -1;
    if (options.keepSeedComponentOnly) {
        for (int seed : seedIndices) {
            const int local = classified.indexOf(seed);
            if (local < 0) {
                result.error = QStringLiteral("拟合后采样点不在最终平面容差内");
                return result;
            }
            if (selectedComponent < 0) selectedComponent = component[local];
            else if (selectedComponent != component[local]) {
                result.error = QStringLiteral("三个采样点不在同一连通区域");
                return result;
            }
        }
    }
    const int referenceComponentSize = selectedComponent >= 0
        ? componentSizes[selectedComponent]
        : (componentSizes.isEmpty()
            ? 0 : *std::max_element(componentSizes.cbegin(), componentSizes.cend()));
    const int disconnectedPointThreshold = qMax(
        qMax(1, options.minimumDisconnectedComponentPoints),
        int(std::ceil(double(referenceComponentSize)
                      * qBound(0.0f, options.minimumDisconnectedComponentRatio, 1.0f))));
    result.significantComponentCount = selectedComponent >= 0 ? 1 : 0;
    for (int local = 0; local < classified.size(); ++local) {
        if (!options.keepSeedComponentOnly || component[local] == selectedComponent)
            result.planeIndices.push_back(classified[local]);
        else if (componentSizes[component[local]] >= disconnectedPointThreshold)
            result.disconnectedPlaneIndices.push_back(classified[local]);
    }
    for (int componentIndex = 0; componentIndex < componentSizes.size(); ++componentIndex) {
        if (componentIndex != selectedComponent
            && componentSizes[componentIndex] >= disconnectedPointThreshold)
            ++result.significantComponentCount;
    }
    if (result.planeIndices.size() < requiredInliers) {
        result.error = QStringLiteral("目标连通区域点数不足：%1 / %2")
                           .arg(result.planeIndices.size()).arg(requiredInliers);
        return result;
    }

    result.planePoints.reserve(result.planeIndices.size());
    double squaredError = 0.0;
    for (int index : result.planeIndices) {
        const Point3D &p = points[index];
        result.planePoints.push_back(p);
        const float distance = residual(finalNormal, finalD, p);
        squaredError += double(distance) * distance;
    }
    result.rmsError = float(std::sqrt(squaredError / double(result.planeIndices.size())));
    result.usedThreshold = surfaceTolerance;
    result.model = {finalNormal.x(), finalNormal.y(), finalNormal.z(), finalD,
                    int(result.planeIndices.size()), result.rmsError, surfaceTolerance};
    result.ok = true;
    return result;
}

PlaneEdgeResult segmentPlaneEdges(const QVector<Point3D> &points,
                                  const QVector<int> &planeIndices,
                                  const PlaneModel &model,
                                  const PlaneEdgeOptions &options) {
    PlaneEdgeResult result;
    if (points.isEmpty() || planeIndices.size() < 3) {
        result.error = QStringLiteral("需要已提取且包含至少三个点的平面");
        return result;
    }
    QVector3D normal(model.a, model.b, model.c);
    const float normalLength = normal.length();
    if (!std::isfinite(normalLength) || normalLength <= 1.0e-8f) {
        result.error = QStringLiteral("平面模型无效");
        return result;
    }
    normal /= normalLength;
    if (normal.z() < 0.0f) normal = -normal;
    const float normalizedD = model.d / normalLength
        * (QVector3D::dotProduct(normal, QVector3D(model.a, model.b, model.c)) >= 0.0f
               ? 1.0f : -1.0f);
    const QVector3D origin = -normalizedD * normal;
    QVector3D axisU = QVector3D::crossProduct(normal,
        std::abs(normal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
    if (axisU.lengthSquared() <= 1.0e-12f) axisU = QVector3D(1, 0, 0);
    axisU.normalize();
    const QVector3D axisV = QVector3D::crossProduct(normal, axisU).normalized();

    QVector<QVector2D> projected;
    QVector<int> validIndices;
    projected.reserve(planeIndices.size());
    validIndices.reserve(planeIndices.size());
    float minimumU = std::numeric_limits<float>::max();
    float minimumV = std::numeric_limits<float>::max();
    float maximumU = std::numeric_limits<float>::lowest();
    float maximumV = std::numeric_limits<float>::lowest();
    for (int index : planeIndices) {
        if (index < 0 || index >= points.size()) continue;
        const Point3D &point = points[index];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            continue;
        const QVector3D delta(QVector3D(point.x, point.y, point.z) - origin);
        const QVector2D uv(QVector3D::dotProduct(delta, axisU),
                           QVector3D::dotProduct(delta, axisV));
        projected.push_back(uv);
        validIndices.push_back(index);
        minimumU = qMin(minimumU, uv.x()); maximumU = qMax(maximumU, uv.x());
        minimumV = qMin(minimumV, uv.y()); maximumV = qMax(maximumV, uv.y());
    }
    if (projected.size() < 3) {
        result.error = QStringLiteral("平面索引没有对应到当前画布缓存");
        return result;
    }

    const double area = double(qMax(maximumU - minimumU, 1.0e-6f))
        * double(qMax(maximumV - minimumV, 1.0e-6f));
    const float estimatedSpacing = qMax(
        float(std::sqrt(area / qMax(1, projected.size()))), 1.0e-6f);
    float gridSize = options.edgeGridSize > 0.0f
        ? options.edgeGridSize : estimatedSpacing * 1.25f;
    const int closeRadius = qBound(0, options.morphologyCloseRadius, 4);
    const int openRadius = qBound(0, options.morphologyOpenRadius, 4);
    const int padding = qMax(2, closeRadius + openRadius + 2);
    const qsizetype maximumCells = qMax(1024, options.maximumEdgeGridCells);
    int gridWidth = 0;
    int gridHeight = 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const double widthValue = std::ceil(double(maximumU - minimumU) / gridSize)
            + 1.0 + 2.0 * padding;
        const double heightValue = std::ceil(double(maximumV - minimumV) / gridSize)
            + 1.0 + 2.0 * padding;
        if (widthValue <= std::numeric_limits<int>::max()
            && heightValue <= std::numeric_limits<int>::max()
            && widthValue * heightValue <= double(maximumCells)) {
            gridWidth = int(widthValue);
            gridHeight = int(heightValue);
            break;
        }
        gridSize *= 2.0f;
    }
    if (gridWidth <= 2 || gridHeight <= 2) {
        result.error = QStringLiteral("平面范围过大，无法在栅格上生成边缘");
        return result;
    }

    minimumU -= float(padding) * gridSize;
    minimumV -= float(padding) * gridSize;
    QVector<quint8> mask(gridWidth * gridHeight, 0);
    QVector<int> pointCells(projected.size(), -1);
    for (int local = 0; local < projected.size(); ++local) {
        const int x = qBound(0, int(std::floor((projected[local].x() - minimumU) / gridSize)),
                             gridWidth - 1);
        const int y = qBound(0, int(std::floor((projected[local].y() - minimumV) / gridSize)),
                             gridHeight - 1);
        pointCells[local] = y * gridWidth + x;
        mask[pointCells[local]] = 1;
    }
    if (closeRadius > 0)
        mask = erodeMask(dilateMask(mask, gridWidth, gridHeight, closeRadius),
                         gridWidth, gridHeight, closeRadius);
    if (openRadius > 0)
        mask = dilateMask(erodeMask(mask, gridWidth, gridHeight, openRadius),
                          gridWidth, gridHeight, openRadius);

    QVector<quint8> boundaryMask(gridWidth * gridHeight, 0);
    for (int y = 0; y < gridHeight; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            const int cell = y * gridWidth + x;
            if (!mask[cell]) continue;
            ++result.occupiedCellCount;
            for (int dy = -1; dy <= 1 && !boundaryMask[cell]; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= gridWidth || ny < 0 || ny >= gridHeight
                        || !mask[ny * gridWidth + nx]) {
                        boundaryMask[cell] = 1;
                        break;
                    }
                }
        }
    }
    for (int local = 0; local < validIndices.size(); ++local)
        if (pointCells[local] >= 0 && boundaryMask[pointCells[local]])
            result.edgeIndices.push_back(validIndices[local]);

    QVector<QVector<QVector2D>> contourUvs = marchingSquaresContours(
        mask, gridWidth, gridHeight, gridSize, minimumU, minimumV);
    QVector<double> contourAreas;
    contourAreas.reserve(contourUvs.size());
    for (const auto &contour : contourUvs) contourAreas.push_back(signedArea(contour));
    for (int contourIndex = 0; contourIndex < contourUvs.size(); ++contourIndex) {
        const QVector<QVector2D> &contourUv = contourUvs[contourIndex];
        if (std::abs(contourAreas[contourIndex]) < gridSize * gridSize) continue;
        int nestingDepth = 0;
        for (int other = 0; other < contourUvs.size(); ++other) {
            if (other == contourIndex
                || std::abs(contourAreas[other]) <= std::abs(contourAreas[contourIndex]))
                continue;
            if (pointInPolygon(contourUv.first(), contourUvs[other])) ++nestingDepth;
        }
        PlaneContour contour;
        contour.hole = (nestingDepth % 2) != 0;
        contour.points.reserve(contourUv.size());
        for (const QVector2D &uv : contourUv) {
            const QVector3D position = origin + axisU * uv.x() + axisV * uv.y();
            contour.points.push_back({position.x(), position.y(), position.z(),
                                      normal.x(), normal.y(), normal.z()});
        }
        result.contours.push_back(std::move(contour));
    }

    result.image = QImage(gridWidth, gridHeight, QImage::Format_Grayscale8);
    // Edge output is a single-channel mask: workpiece=225, background=0.
    result.image.fill(0);
    for (int y = 0; y < gridHeight; ++y) {
        uchar *row = result.image.scanLine(gridHeight - 1 - y);
        for (int x = 0; x < gridWidth; ++x) {
            const int cell = y * gridWidth + x;
            if (mask[cell]) row[x] = 225;
        }
    }
    result.gridSize = gridSize;
    result.width = maximumU - minimumU + float(padding) * gridSize;
    result.height = maximumV - minimumV + float(padding) * gridSize;
    result.ok = !result.image.isNull();
    if (!result.ok) result.error = QStringLiteral("无法创建平面 2D 图像");
    return result;
}

ObstacleDetectionResult detectObstacles(const QVector<Point3D> &points,
                                        const QVector<int> &planeIndices,
                                        const PlaneModel &model,
                                        const ObstacleDetectionOptions &options) {
    ObstacleDetectionResult result;
    if (points.isEmpty() || planeIndices.size() < 3) {
        result.error = QStringLiteral("需要已确定且包含至少三个点的基准平面");
        return result;
    }
    if (!std::isfinite(options.minimumHeight) || options.minimumHeight <= 0.0f
        || options.minimumPointCount < 1 || !std::isfinite(options.minimumArea)
        || options.minimumArea < 0.0f || options.connectivityRadiusCells < 1
        || options.connectivityRadiusCells > 4) {
        result.error = QStringLiteral("障碍物检测参数无效");
        return result;
    }

    QVector3D normal(model.a, model.b, model.c);
    const float normalLength = normal.length();
    if (!std::isfinite(normalLength) || normalLength <= 1.0e-8f) {
        result.error = QStringLiteral("基准平面模型无效");
        return result;
    }
    normal /= normalLength;
    float normalizedD = model.d / normalLength;
    if (normal.z() < 0.0f) {
        normal = -normal;
        normalizedD = -normalizedD;
    }
    const QVector3D origin = -normalizedD * normal;
    QVector3D axisU = QVector3D::crossProduct(
        normal, std::abs(normal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
    if (axisU.lengthSquared() <= 1.0e-12f) axisU = QVector3D(1, 0, 0);
    axisU.normalize();
    const QVector3D axisV = QVector3D::crossProduct(normal, axisU).normalized();

    float minimumU = std::numeric_limits<float>::max();
    float minimumV = std::numeric_limits<float>::max();
    float maximumU = std::numeric_limits<float>::lowest();
    float maximumV = std::numeric_limits<float>::lowest();
    int validPlanePointCount = 0;
    for (int index : planeIndices) {
        if (index < 0 || index >= points.size() || !usablePoint(points[index])) continue;
        const QVector3D point(points[index].x, points[index].y, points[index].z);
        const QVector3D delta = point - origin;
        const float u = QVector3D::dotProduct(delta, axisU);
        const float v = QVector3D::dotProduct(delta, axisV);
        minimumU = qMin(minimumU, u); maximumU = qMax(maximumU, u);
        minimumV = qMin(minimumV, v); maximumV = qMax(maximumV, v);
        ++validPlanePointCount;
    }
    if (validPlanePointCount < 3 || maximumU <= minimumU || maximumV <= minimumV) {
        result.error = QStringLiteral("基准平面没有有效的二维范围");
        return result;
    }

    const double footprintArea = double(maximumU - minimumU) * double(maximumV - minimumV);
    float gridSize = options.gridSize > 0.0f
        ? options.gridSize
        : qMax(float(std::sqrt(footprintArea / double(validPlanePointCount)) * 2.0), 1.0e-6f);
    const int maximumCells = qMax(1024, options.maximumGridCells);
    int gridWidth = 0;
    int gridHeight = 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const double width = std::ceil(double(maximumU - minimumU) / gridSize) + 1.0;
        const double height = std::ceil(double(maximumV - minimumV) / gridSize) + 1.0;
        if (width <= std::numeric_limits<int>::max()
            && height <= std::numeric_limits<int>::max()
            && width * height <= double(maximumCells)) {
            gridWidth = int(width);
            gridHeight = int(height);
            break;
        }
        gridSize *= 2.0f;
    }
    if (gridWidth <= 0 || gridHeight <= 0) {
        result.error = QStringLiteral("平面范围过大，无法建立障碍物检测栅格");
        return result;
    }

    const int gridCellCount = gridWidth * gridHeight;
    std::unordered_map<int, QVector<int>> occupiedCells;
    occupiedCells.reserve(std::size_t(qMin(points.size(), maximumCells)));
    const float footprintMargin = gridSize;
    for (int index = 0; index < points.size(); ++index) {
        const Point3D &source = points[index];
        if (!usablePoint(source)) continue;
        const QVector3D point(source.x, source.y, source.z);
        const float signedHeight = QVector3D::dotProduct(normal, point) + normalizedD;
        if (!std::isfinite(signedHeight)
            || std::abs(signedHeight) < options.minimumHeight)
            continue;
        const QVector3D delta = point - origin;
        const float u = QVector3D::dotProduct(delta, axisU);
        const float v = QVector3D::dotProduct(delta, axisV);
        if (u < minimumU - footprintMargin || u > maximumU + footprintMargin
            || v < minimumV - footprintMargin || v > maximumV + footprintMargin)
            continue;
        const int x = qBound(0, int(std::floor((u - minimumU) / gridSize)), gridWidth - 1);
        const int y = qBound(0, int(std::floor((v - minimumV) / gridSize)), gridHeight - 1);
        // Keep both sides topologically independent. A foreground obstacle and
        // a background surface may project to the same UV cell in 2.5D data.
        const bool negativeSide = signedHeight < 0.0f;
        const int sideOffset = negativeSide ? gridCellCount : 0;
        occupiedCells[sideOffset + y * gridWidth + x].push_back(index);
        ++result.candidatePointCount;
        if (negativeSide) ++result.negativeCandidatePointCount;
        else ++result.positiveCandidatePointCount;
    }

    std::unordered_set<int> visited;
    visited.reserve(occupiedCells.size());
    int nextRegionId = 1;
    for (const auto &entry : occupiedCells) {
        const int startCell = entry.first;
        if (!visited.insert(startCell).second) continue;
        const bool negativeSide = startCell >= gridCellCount;
        const int sideOffset = negativeSide ? gridCellCount : 0;
        QVector<int> queue{startCell};
        QVector<int> componentPoints;
        int componentCellCount = 0;
        for (qsizetype head = 0; head < queue.size(); ++head) {
            const int cell = queue[head];
            ++componentCellCount;
            const auto found = occupiedCells.find(cell);
            if (found == occupiedCells.end()) continue;
            componentPoints += found->second;
            const int localCell = cell - sideOffset;
            const int x = localCell % gridWidth;
            const int y = localCell / gridWidth;
            const int radius = options.connectivityRadiusCells;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int neighborX = x + dx;
                    const int neighborY = y + dy;
                    if (neighborX < 0 || neighborX >= gridWidth
                        || neighborY < 0 || neighborY >= gridHeight)
                        continue;
                    const int neighborCell = sideOffset + neighborY * gridWidth + neighborX;
                    if (occupiedCells.find(neighborCell) != occupiedCells.end()
                        && visited.insert(neighborCell).second)
                        queue.push_back(neighborCell);
                }
            }
        }

        const float componentArea = float(componentCellCount) * gridSize * gridSize;
        if (componentPoints.size() < options.minimumPointCount
            || componentArea < options.minimumArea)
            continue;

        ObstacleRegion region;
        region.id = nextRegionId++;
        region.sideSign = negativeSide ? -1 : 1;
        region.pointIndices = std::move(componentPoints);
        region.area = componentArea;
        region.minimumBound = {std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
        region.maximumBound = {std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest()};
        QVector3D centroid;
        double heightSum = 0.0;
        for (int index : region.pointIndices) {
            const Point3D &point = points[index];
            centroid += QVector3D(point.x, point.y, point.z);
            const float height = QVector3D::dotProduct(
                normal, QVector3D(point.x, point.y, point.z)) + normalizedD;
            const float absoluteHeight = std::abs(height);
            heightSum += absoluteHeight;
            region.maximumHeight = qMax(region.maximumHeight, absoluteHeight);
            region.minimumBound.x = qMin(region.minimumBound.x, point.x);
            region.minimumBound.y = qMin(region.minimumBound.y, point.y);
            region.minimumBound.z = qMin(region.minimumBound.z, point.z);
            region.maximumBound.x = qMax(region.maximumBound.x, point.x);
            region.maximumBound.y = qMax(region.maximumBound.y, point.y);
            region.maximumBound.z = qMax(region.maximumBound.z, point.z);
        }
        centroid /= float(region.pointIndices.size());
        region.centroid = {centroid.x(), centroid.y(), centroid.z()};
        region.meanHeight = float(heightSum / double(region.pointIndices.size()));
        result.obstacleIndices += region.pointIndices;
        result.regions.push_back(std::move(region));
    }

    std::sort(result.obstacleIndices.begin(), result.obstacleIndices.end());
    result.obstacleIndices.erase(
        std::unique(result.obstacleIndices.begin(), result.obstacleIndices.end()),
        result.obstacleIndices.end());
    result.gridSize = gridSize;
    result.summary = result.regions.isEmpty()
        ? QStringLiteral("未发现达到阈值的障碍物")
        : QStringLiteral("发现 %1 个障碍物区域，共 %2 个点")
              .arg(result.regions.size()).arg(result.obstacleIndices.size());
    result.ok = true;
    return result;
}

PlaneImageResult extractPlaneImage(const QVector<Point3D> &points,
                                   const QVector<int> &planeIndices,
                                   const PlaneModel &model,
                                   const PlaneEdgeOptions &options) {
    PlaneImageResult result;
    result.inputIndexCount = planeIndices.size();
    if (points.isEmpty() || planeIndices.size() < 3) {
        result.error = QStringLiteral("需要已提取且包含至少三个点的平面");
        return result;
    }
    QVector3D normal(model.a, model.b, model.c);
    const float length = normal.length();
    if (!std::isfinite(length) || length <= 1.0e-8f) {
        result.error = QStringLiteral("平面模型无效");
        return result;
    }
    normal /= length;
    float d = model.d / length;
    if (normal.z() < 0.0f) { normal = -normal; d = -d; }
    QVector3D origin = -d * normal;
    QVector3D axisU;
    QVector3D axisV;
    if (options.useImageFrame && options.imageAxisU.lengthSquared() > 1.0e-10f
        && options.imageAxisV.lengthSquared() > 1.0e-10f) {
        // The user-selected frame is projected onto the fitted plane so a
        // slightly off-surface click cannot shear the exported image.
        origin = options.imageOrigin;
        axisU = options.imageAxisU - normal * QVector3D::dotProduct(options.imageAxisU, normal);
        if (axisU.lengthSquared() <= 1.0e-10f) {
            result.error = QStringLiteral("用户 X 轴点接近平面法向，无法建立图像坐标轴");
            return result;
        }
        axisU.normalize();
        axisV = options.imageAxisV - normal * QVector3D::dotProduct(options.imageAxisV, normal);
        axisV -= axisU * QVector3D::dotProduct(axisV, axisU);
        if (axisV.lengthSquared() <= 1.0e-10f) {
            result.error = QStringLiteral("用户 X/Y 轴点过近或近似共线，无法建立图像坐标轴");
            return result;
        }
        axisV.normalize();
        if (QVector3D::dotProduct(QVector3D::crossProduct(axisU, axisV), normal) < 0.0f)
            axisV = -axisV;
        result.usedWorkpieceFrame = true;
    } else {
        origin = -d * normal;
        axisU = QVector3D::crossProduct(normal,
            std::abs(normal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
        if (axisU.lengthSquared() <= 1.0e-12f) axisU = QVector3D(1, 0, 0);
        axisU.normalize();
        axisV = QVector3D::crossProduct(normal, axisU).normalized();
    }
    QVector<QVector2D> projected;
    projected.reserve(planeIndices.size());
    float minU = std::numeric_limits<float>::max(), minV = minU;
    float maxU = std::numeric_limits<float>::lowest(), maxV = maxU;
    QVector3D mappedMinimum(std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max());
    QVector3D mappedMaximum(std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest());
    const float planeTolerance = qMax(0.0f, options.planeDistanceTolerance);
    result.usedPlaneDistanceTolerance = planeTolerance;
    for (int index : planeIndices) {
        if (index < 0 || index >= points.size() || !usablePoint(points[index])) {
            ++result.rejectedInvalidPointCount;
            continue;
        }
        const QVector3D p(points[index].x, points[index].y, points[index].z);
        const QVector3D delta = p - origin;
        // Explicit base->workpiece mapping.  The third component is the
        // projection on the frame Z axis; the independent signed-plane test
        // below decides whether this point belongs to the fitted surface.
        const float signedPlaneDistance = QVector3D::dotProduct(normal, p) + d;
        const QVector3D workpiecePoint(QVector3D::dotProduct(delta, axisU),
                                       QVector3D::dotProduct(delta, axisV),
                                       QVector3D::dotProduct(delta, normal));
        if (std::abs(signedPlaneDistance) > planeTolerance) {
            ++result.rejectedNonPlanePointCount;
            continue;
        }
        ++result.mappedPlanePointCount;
        mappedMinimum.setX(qMin(mappedMinimum.x(), workpiecePoint.x()));
        mappedMinimum.setY(qMin(mappedMinimum.y(), workpiecePoint.y()));
        mappedMinimum.setZ(qMin(mappedMinimum.z(), workpiecePoint.z()));
        mappedMaximum.setX(qMax(mappedMaximum.x(), workpiecePoint.x()));
        mappedMaximum.setY(qMax(mappedMaximum.y(), workpiecePoint.y()));
        mappedMaximum.setZ(qMax(mappedMaximum.z(), workpiecePoint.z()));
        const QVector2D uv(workpiecePoint.x(), workpiecePoint.y());
        projected.push_back(uv);
        minU = qMin(minU, uv.x()); maxU = qMax(maxU, uv.x());
        minV = qMin(minV, uv.y()); maxV = qMax(maxV, uv.y());
    }
    if (projected.size() < 3) { result.error = QStringLiteral("平面没有有效点"); return result; }
    result.workpieceMinimum = mappedMinimum;
    result.workpieceMaximum = mappedMaximum;
    result.rawWidth = qMax(maxU - minU, 0.0f);
    result.rawHeight = qMax(maxV - minV, 0.0f);
    const bool automaticBounds = options.autoImageBounds && options.useImageFrame;
    result.automaticBounds = automaticBounds;
    float gridSize = options.edgeGridSize > 0.0f ? options.edgeGridSize : 0.05f;
    int padding = 2;
    if (automaticBounds) {
        const float margin = qMax(0.0f, options.imageMargin);
        const float increment = qMax(1.0e-6f, options.imageRoundIncrement);
        const float halfU = qMax(std::abs(minU), std::abs(maxU)) + margin;
        const float halfV = qMax(std::abs(minV), std::abs(maxV)) + margin;
        const float width = std::ceil((2.0f * halfU) / increment - 1.0e-6f) * increment;
        const float height = std::ceil((2.0f * halfV) / increment - 1.0e-6f) * increment;
        minU = -width * 0.5f;
        maxU = width * 0.5f;
        minV = -height * 0.5f;
        maxV = height * 0.5f;
        gridSize = qMax(1.0e-6f, options.imagePixelSize);
        padding = 0;
        result.margin = margin;
        result.pixelSize = gridSize;
    } else {
        const bool cropToRectangle = options.useImageFrame
            && options.imageCropWidth > 0.0f && options.imageCropHeight > 0.0f;
        if (cropToRectangle) {
            minU = -options.imageCropWidth * 0.5f;
            maxU = options.imageCropWidth * 0.5f;
            minV = -options.imageCropHeight * 0.5f;
            maxV = options.imageCropHeight * 0.5f;
            padding = 0;
        }
        const double area = double(qMax(maxU - minU, 1.0e-6f))
            * double(qMax(maxV - minV, 1.0e-6f));
        if (options.edgeGridSize <= 0.0f)
            gridSize = qMax(float(std::sqrt(area / projected.size()) * 1.25), 1.0e-6f);
        result.pixelSize = gridSize;
    }
    const qsizetype maxCells = automaticBounds
        ? qMax<qsizetype>(1024, options.maximumImagePixels)
        : qMax<qsizetype>(1024, options.maximumEdgeGridCells);
    int width = 0, height = 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const double w = std::ceil(double(maxU - minU) / gridSize) + 1 + 2 * padding;
        const double h = std::ceil(double(maxV - minV) / gridSize) + 1 + 2 * padding;
        if (w <= std::numeric_limits<int>::max() && h <= std::numeric_limits<int>::max()
            && w * h <= double(maxCells)) { width = int(w); height = int(h); break; }
        if (automaticBounds) {
            result.error = QStringLiteral("自动边界按 0.05 mm/px 生成的图像过大，预计像素数超过限制");
            return result;
        }
        gridSize *= 2.0f;
    }
    if (width <= 2 || height <= 2) { result.error = QStringLiteral("平面范围过大，无法生成 2D 图像"); return result; }
    minU -= padding * gridSize; minV -= padding * gridSize;
    QVector<quint8> valid(width * height, 0);
    for (const QVector2D &uv : projected) {
        if (uv.x() < minU || uv.x() > maxU
                                || uv.y() < minV || uv.y() > maxV) {
            ++result.rejectedOutsideRectangleCount;
            continue;
        }
        const int x = qBound(0, int(std::floor((uv.x() - minU) / gridSize)), width - 1);
        const int y = qBound(0, int(std::floor((uv.y() - minV) / gridSize)), height - 1);
        valid[y * width + x] = 1;
    }
    const int imageScale = automaticBounds ? 1 : qBound(1, options.imageScale, 8);
    QImage baseImage(width, height, QImage::Format_Grayscale8);
    result.image = baseImage;
    // The exported plane image is a data product rather than a UI preview:
    // non-workpiece pixels must remain pure black for deterministic OpenCV masks.
    result.image.fill(qRgb(0, 0, 0));
    int occupied = 0;
    for (int y = 0; y < height; ++y) {
        uchar *row = result.image.scanLine(height - 1 - y);
        for (int x = 0; x < width; ++x) {
            if (!valid[y * width + x]) continue;
            ++occupied;
            row[x] = 225;
        }
    }
    if (imageScale > 1) {
        const int imageWidth = qMin(std::numeric_limits<int>::max() / imageScale, width * imageScale);
        const int imageHeight = qMin(std::numeric_limits<int>::max() / imageScale, height * imageScale);
        result.image = result.image.scaled(imageWidth, imageHeight, Qt::IgnoreAspectRatio,
                                           Qt::FastTransformation);
    }
    result.gridSize = gridSize;
    result.width = maxU - minU + padding * gridSize;
    result.height = maxV - minV + padding * gridSize;
    if (automaticBounds) {
        result.width = maxU - minU;
        result.height = maxV - minV;
    }
    result.occupiedCellCount = occupied;
    result.ok = !result.image.isNull();
    if (!result.ok) result.error = QStringLiteral("无法创建平面 2D 图像");
    return result;
}

PlanePlyExportResult exportPlanePly(const QString &fileName,
                                    const QVector<Point3D> &points,
                                    const QVector<int> &planeIndices,
                                    const PlaneModel &model,
                                    const WorkpieceCoordinateSystem &frame,
                                    const QString &sourceFile) {
    PlanePlyExportResult result;
    result.filePath = fileName;
    if (fileName.isEmpty() || points.isEmpty() || planeIndices.isEmpty()) {
        result.error = QStringLiteral("没有可导出的平面点或输出路径为空");
        return result;
    }
    QVector<int> valid;
    valid.reserve(planeIndices.size());
    for (const int index : planeIndices) {
        if (index >= 0 && index < points.size() && usablePoint(points[index]))
            valid.push_back(index);
    }
    if (valid.isEmpty()) {
        result.error = QStringLiteral("平面索引中没有有效点");
        return result;
    }
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QVector3D n(model.a, model.b, model.c);
    const QString source = sourceFile.isEmpty() ? QStringLiteral("unknown") : sourceFile;
    QTextStream header(&file);
    header.setEncoding(QStringConverter::Utf8);
    header << "ply\nformat binary_little_endian 1.0\n"
           << "comment source_frame robot_base\n"
           << "comment target_frame workpiece\n"
           << "comment source_file " << source << "\n"
           << "comment plane_equation " << model.a << " " << model.b << " "
           << model.c << " " << model.d << "\n"
           << "comment workpiece_origin_robot_base "
           << frame.originInRobotBase.x() << " " << frame.originInRobotBase.y() << " "
           << frame.originInRobotBase.z() << "\n"
           << "comment workpiece_pose_abc " << frame.poseA << " " << frame.poseB << " "
           << frame.poseC << "\n"
           << "element vertex " << valid.size() << "\n"
           << "property float x\nproperty float y\nproperty float z\n"
           << "property float nx\nproperty float ny\nproperty float nz\n"
           << "property uint source_index\nend_header\n";
    header.flush();
    if (header.status() != QTextStream::Ok) {
        result.error = QStringLiteral("写入 PLY 头失败");
        return result;
    }
    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    data.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (const int index : valid) {
        const Point3D &p = points[index];
        data << p.x << p.y << p.z << p.nx << p.ny << p.nz
             << quint32(index);
    }
    if (data.status() != QDataStream::Ok || !file.commit()) {
        result.error = file.errorString().isEmpty()
            ? QStringLiteral("写入 PLY 数据失败") : file.errorString();
        return result;
    }
    result.exportedPointCount = valid.size();
    result.ok = true;
    return result;
}

ThreePointPlaneResult selectPlaneFromThreeSeeds(const QVector<Point3D> &points,
                                                const QVector<int> &seedIndices,
                                                const ThreePointPlaneOptions &options) {
    return extractPlaneFromThreePoints(points, seedIndices, options);
}

WorkpieceCoordinateSystem buildWorkpieceCoordinateSystem(
    const QVector<Point3D> &points, const QVector<int> &seedIndices,
    int originIndex, bool preferPositiveZ) {
    WorkpieceCoordinateSystem result;
    if (seedIndices.size() != 3) {
        result.error = QStringLiteral("建立工件坐标系必须提供三个点");
        return result;
    }
    const auto validIndex = [&points](int index) {
        return index >= 0 && index < points.size();
    };
    if (!validIndex(seedIndices[0]) || !validIndex(seedIndices[1])
        || !validIndex(seedIndices[2])) {
        result.error = QStringLiteral("工件坐标系点索引超出当前画布缓存");
        return result;
    }
    const auto position = [&points](int index) {
        const Point3D &point = points[index];
        return QVector3D(point.x, point.y, point.z);
    };
    const QVector3D p1 = position(seedIndices[0]);
    const QVector3D p2 = position(seedIndices[1]);
    const QVector3D p3 = position(seedIndices[2]);
    const QVector3D first = p2 - p1;
    const QVector3D second = p3 - p1;
    if (first.lengthSquared() < 1.0e-8f || second.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("工件坐标系点之间距离过近");
        return result;
    }
    QVector3D z = QVector3D::crossProduct(first, second);
    if (z.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("三个点近似共线，无法建立工件坐标系");
        return result;
    }
    z.normalize();
    if (preferPositiveZ && z.z() < 0.0f) z = -z;
    QVector3D x = first - z * QVector3D::dotProduct(first, z);
    if (x.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("第一、第二点方向无法形成稳定工件 X 轴");
        return result;
    }
    x.normalize();
    QVector3D y = QVector3D::crossProduct(z, x);
    if (y.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("工件 Y 轴计算失败");
        return result;
    }
    y.normalize();
    const int actualOriginIndex = originIndex >= 0 ? originIndex : seedIndices[0];
    if (!validIndex(actualOriginIndex)) {
        result.error = QStringLiteral("工件原点索引超出当前画布缓存");
        return result;
    }
    const QVector3D origin = position(actualOriginIndex);
    result.originInRobotBase = origin;
    result.axisXInRobotBase = x;
    result.axisYInRobotBase = y;
    result.axisZInRobotBase = z;
    result.orthogonalityError = qMax(qMax(std::abs(QVector3D::dotProduct(x, y)),
                                          std::abs(QVector3D::dotProduct(x, z))),
                                     std::abs(QVector3D::dotProduct(y, z)));
    result.workpieceToRobotBase.setToIdentity();
    for (int row = 0; row < 3; ++row) {
        result.workpieceToRobotBase(row, 0) = x[row];
        result.workpieceToRobotBase(row, 1) = y[row];
        result.workpieceToRobotBase(row, 2) = z[row];
        result.workpieceToRobotBase(row, 3) = origin[row];
    }
    bool invertible = false;
    result.robotBaseToWorkpiece = result.workpieceToRobotBase.inverted(&invertible);
    if (!invertible) {
        result.error = QStringLiteral("工件坐标系矩阵不可逆");
        return result;
    }
    const double ry = std::asin(std::clamp(
        -double(result.workpieceToRobotBase(2, 0)), -1.0, 1.0));
    const double cosRy = std::cos(ry);
    double rx = 0.0;
    double rz = 0.0;
    if (std::abs(cosRy) > 1.0e-8) {
        rx = std::atan2(double(result.workpieceToRobotBase(2, 1)),
                        double(result.workpieceToRobotBase(2, 2)));
        rz = std::atan2(double(result.workpieceToRobotBase(1, 0)),
                        double(result.workpieceToRobotBase(0, 0)));
    } else {
        rz = std::atan2(-double(result.workpieceToRobotBase(0, 1)),
                        double(result.workpieceToRobotBase(1, 1)));
    }
    result.poseA = float(qRadiansToDegrees(rx));
    result.poseB = float(qRadiansToDegrees(ry));
    result.poseC = float(qRadiansToDegrees(rz));
    result.valid = true;
    return result;
}

WorkpieceCoordinateSystem buildWorkpieceCoordinateSystem(
    const QVector<Point3D> &points, const QVector3D &origin,
    const QVector3D &planeNormal, int xAxisPointIndex, int yAxisPointIndex,
    bool preferPositiveZ) {
    WorkpieceCoordinateSystem result;
    const auto validIndex = [&points](int index) {
        return index >= 0 && index < points.size();
    };
    if (!validIndex(xAxisPointIndex) || !validIndex(yAxisPointIndex)) {
        result.error = QStringLiteral("X/Y 轴点索引超出当前画布缓存");
        return result;
    }
    const auto position = [&points](int index) {
        const Point3D &p = points[index];
        return QVector3D(p.x, p.y, p.z);
    };
    QVector3D z = planeNormal;
    if (z.lengthSquared() < 1.0e-10f) {
        result.error = QStringLiteral("拟合平面法向量无效");
        return result;
    }
    z.normalize();
    if (preferPositiveZ && z.z() < 0.0f) z = -z;
    QVector3D x = position(xAxisPointIndex) - origin;
    QVector3D yRaw = position(yAxisPointIndex) - origin;
    // Both user directions are projected onto the fitted plane. Z remains the
    // fitted plane normal instead of being inferred from noisy picked points.
    x -= z * QVector3D::dotProduct(x, z);
    yRaw -= z * QVector3D::dotProduct(yRaw, z);
    if (x.lengthSquared() < 1.0e-8f || yRaw.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("X/Y 轴点不能与平面中心重合");
        return result;
    }
    x.normalize();
    const QVector3D yDirection = yRaw - x * QVector3D::dotProduct(yRaw, x);
    if (yDirection.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("X/Y 轴点近似共线");
        return result;
    }
    QVector3D y = QVector3D::crossProduct(z, x);
    if (y.lengthSquared() < 1.0e-8f) {
        result.error = QStringLiteral("X/Y 轴点过近或近似共线");
        return result;
    }
    y.normalize();
    // The Y point selects the normal sign. This preserves a right-handed frame
    // while keeping Z exactly perpendicular to the extracted plane.
    if (QVector3D::dotProduct(y, yDirection) < 0.0f) {
        z = -z;
        y = -y;
    }
    result.originInRobotBase = origin;
    result.axisXInRobotBase = x;
    result.axisYInRobotBase = y;
    result.axisZInRobotBase = z;
    result.orthogonalityError = qMax(qMax(std::abs(QVector3D::dotProduct(x, y)),
                                          std::abs(QVector3D::dotProduct(x, z))),
                                     std::abs(QVector3D::dotProduct(y, z)));
    result.workpieceToRobotBase.setToIdentity();
    for (int row = 0; row < 3; ++row) {
        result.workpieceToRobotBase(row, 0) = x[row];
        result.workpieceToRobotBase(row, 1) = y[row];
        result.workpieceToRobotBase(row, 2) = z[row];
        result.workpieceToRobotBase(row, 3) = origin[row];
    }
    bool invertible = false;
    result.robotBaseToWorkpiece = result.workpieceToRobotBase.inverted(&invertible);
    if (!invertible) {
        result.error = QStringLiteral("工件坐标系矩阵不可逆");
        return result;
    }
    const double ry = std::asin(std::clamp(-double(result.workpieceToRobotBase(2, 0)), -1.0, 1.0));
    const double cosRy = std::cos(ry);
    double rx = 0.0, rz = 0.0;
    if (std::abs(cosRy) > 1.0e-8) {
        rx = std::atan2(double(result.workpieceToRobotBase(2, 1)),
                        double(result.workpieceToRobotBase(2, 2)));
        rz = std::atan2(double(result.workpieceToRobotBase(1, 0)),
                        double(result.workpieceToRobotBase(0, 0)));
    } else {
        rz = std::atan2(-double(result.workpieceToRobotBase(0, 1)),
                        double(result.workpieceToRobotBase(1, 1)));
    }
    result.poseA = float(qRadiansToDegrees(rx));
    result.poseB = float(qRadiansToDegrees(ry));
    result.poseC = float(qRadiansToDegrees(rz));
    result.valid = true;
    return result;
}

} // namespace pointcloud
