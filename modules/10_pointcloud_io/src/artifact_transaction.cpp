#include <pcv/io/artifact_transaction.h>

#include <pcv/io/ply_writer.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

namespace {

bool invalidPathComponent(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty() || text == QStringLiteral(".") || text == QStringLiteral("..")
        || text.contains(QStringLiteral("..")) || text.contains(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\')) || QFileInfo(text).isAbsolute()) return true;
    for (const QChar c : QStringLiteral("<>:\"|?*"))
        if (text.contains(c)) return true;
    return text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(' '));
}

bool writePng(const QString &path, const QImage &source, QString *error)
{
    QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    if (image.isNull()) { if (error) *error = QStringLiteral("无法转换为 Grayscale8"); return false; }
    for (int y = 0; y < image.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) row[x] = row[x] == 0 ? 0 : 255;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString().isEmpty() ? QStringLiteral("PNG 写入失败") : file.errorString();
        return false;
    }
    return true;
}

} // namespace

namespace pcv::io {

ArtifactTransaction::ArtifactTransaction(QString outputRoot, bool allowOverwrite,
                                         std::function<bool()> isCancelled)
    : m_outputRoot(QDir::cleanPath(QFileInfo(std::move(outputRoot)).absoluteFilePath())),
      m_allowOverwrite(allowOverwrite), m_isCancelled(std::move(isCancelled))
{
}

ArtifactTransaction::~ArtifactTransaction()
{
    if (!m_committed) abort();
}

bool ArtifactTransaction::cancelled()
{
    if (m_isCancelled && m_isCancelled()) {
        m_error = QStringLiteral("已取消");
        return true;
    }
    return false;
}

bool ArtifactTransaction::ensureStagingDirectory()
{
    if (!m_stagingRoot.isEmpty()) return true;
    if (m_outputRoot.isEmpty() || !QDir().mkpath(m_outputRoot)) {
        m_error = QStringLiteral("无法创建输出根目录"); return false;
    }
    m_stagingRoot = QDir(m_outputRoot).filePath(QStringLiteral(".pcv_transaction_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(m_stagingRoot)) {
        m_error = QStringLiteral("无法创建事务临时目录"); return false;
    }
    return true;
}

bool ArtifactTransaction::validateRelativePath(const QString &relativePath, QString *absolutePath) const
{
    const QString clean = QDir::cleanPath(relativePath.trimmed());
    if (clean.isEmpty() || clean == QStringLiteral(".") || QFileInfo(clean).isAbsolute()) return false;
    const QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    for (const QString &part : parts) if (invalidPathComponent(part)) return false;
    const QString resolved = QDir::cleanPath(QDir(m_outputRoot).absoluteFilePath(clean));
    const QString root = QDir::cleanPath(m_outputRoot);
    const QString normalizedResolved = QDir::fromNativeSeparators(resolved);
    const QString normalizedRoot = QDir::fromNativeSeparators(root).trimmed();
    if (!(normalizedResolved.compare(normalizedRoot, Qt::CaseInsensitive) == 0
          || normalizedResolved.startsWith(normalizedRoot + QLatin1Char('/'), Qt::CaseInsensitive))) return false;
    if (absolutePath) *absolutePath = resolved;
    return true;
}

bool ArtifactTransaction::writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = file.errorString().isEmpty() ? QStringLiteral("文件写入失败") : file.errorString();
        return false;
    }
    return true;
}

bool ArtifactTransaction::stageBinaryPly(const QString &relativePath,
                                         const QVector<pointcloud::Point3D> &points,
                                         bool includeNormals, const QStringList &comments)
{
    QString destination;
    if (cancelled()) return false;
    if (!validateRelativePath(relativePath, &destination)) { m_error = QStringLiteral("事务相对路径无效"); return false; }
    if (!ensureStagingDirectory()) return false;
    const QString staged = QDir(m_stagingRoot).filePath(QDir::cleanPath(relativePath));
    if (!QDir().mkpath(QFileInfo(staged).absolutePath())) { m_error = QStringLiteral("无法创建事务子目录"); return false; }
    pcv::detail::io::BinaryPlyWriteOptions options;
    options.includeNormals = includeNormals;
    options.comments = comments;
    options.isCancelled = m_isCancelled;
    if (!pcv::detail::io::writeBinaryPly(staged, points, &m_error, options)) return false;
    m_stagedRelativePaths.append(QDir::cleanPath(relativePath));
    return true;
}

bool ArtifactTransaction::stageGrayscalePng(const QString &relativePath, const QImage &image)
{
    QString destination;
    if (cancelled() || !validateRelativePath(relativePath, &destination) || !ensureStagingDirectory()) {
        if (m_error.isEmpty()) m_error = QStringLiteral("事务相对路径无效"); return false;
    }
    const QString staged = QDir(m_stagingRoot).filePath(QDir::cleanPath(relativePath));
    if (!QDir().mkpath(QFileInfo(staged).absolutePath())) { m_error = QStringLiteral("无法创建事务子目录"); return false; }
    if (!writePng(staged, image, &m_error)) return false;
    m_stagedRelativePaths.append(QDir::cleanPath(relativePath));
    return true;
}

bool ArtifactTransaction::stageJson(const QString &relativePath, const QByteArray &json)
{
    return stageBytes(relativePath, json);
}

bool ArtifactTransaction::stageBytes(const QString &relativePath, const QByteArray &bytes)
{
    QString destination;
    if (cancelled() || !validateRelativePath(relativePath, &destination) || !ensureStagingDirectory()) {
        if (m_error.isEmpty()) m_error = QStringLiteral("事务相对路径无效"); return false;
    }
    const QString staged = QDir(m_stagingRoot).filePath(QDir::cleanPath(relativePath));
    if (!QDir().mkpath(QFileInfo(staged).absolutePath())) { m_error = QStringLiteral("无法创建事务子目录"); return false; }
    if (!writeFile(staged, bytes)) return false;
    m_stagedRelativePaths.append(QDir::cleanPath(relativePath));
    return true;
}

bool ArtifactTransaction::rollbackCommitted(const QVector<QString> &destinations,
                                            const QVector<QString> &backups)
{
    for (int i = destinations.size() - 1; i >= 0; --i) {
        QFile::remove(destinations.at(i));
        if (!backups.at(i).isEmpty()) QFile::rename(backups.at(i), destinations.at(i));
    }
    return true;
}

bool ArtifactTransaction::commit()
{
    if (m_committed) return true;
    if (cancelled()) return false;
    if (m_stagedRelativePaths.isEmpty()) { m_error = QStringLiteral("事务没有待提交文件"); return false; }
    QVector<QString> destinations;
    QVector<QString> backups;
    const QString backupRoot = QDir(m_outputRoot).filePath(QStringLiteral(".pcv_backup_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(backupRoot)) { m_error = QStringLiteral("无法创建事务备份目录"); return false; }
    for (const QString &relative : m_stagedRelativePaths) {
        if (cancelled()) { rollbackCommitted(destinations, backups); QDir(backupRoot).removeRecursively(); return false; }
        QString destination;
        if (!validateRelativePath(relative, &destination)) { m_error = QStringLiteral("事务相对路径无效"); rollbackCommitted(destinations, backups); QDir(backupRoot).removeRecursively(); return false; }
        if (QFileInfo::exists(destination) && !m_allowOverwrite) {
            m_error = QStringLiteral("目标文件已存在且不允许覆盖：%1").arg(relative);
            rollbackCommitted(destinations, backups); QDir(backupRoot).removeRecursively(); return false;
        }
        const QString staged = QDir(m_stagingRoot).filePath(relative);
        const QString backup = QDir(backupRoot).filePath(relative);
        QString backupPath;
        if (QFileInfo::exists(destination)) {
            if (!QDir().mkpath(QFileInfo(backup).absolutePath()) || !QFile::rename(destination, backup)) {
                m_error = QStringLiteral("无法备份既有输出：%1").arg(relative);
                rollbackCommitted(destinations, backups); QDir(backupRoot).removeRecursively(); return false;
            }
            backupPath = backup;
        }
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::rename(staged, destination)) {
            m_error = QStringLiteral("无法提交输出：%1").arg(relative);
            destinations.append(destination); backups.append(backupPath);
            rollbackCommitted(destinations, backups); QDir(backupRoot).removeRecursively(); return false;
        }
        destinations.append(destination); backups.append(backupPath);
    }
    QDir(m_stagingRoot).removeRecursively();
    QDir(backupRoot).removeRecursively();
    m_committed = true;
    return true;
}

void ArtifactTransaction::abort()
{
    if (!m_stagingRoot.isEmpty()) QDir(m_stagingRoot).removeRecursively();
}

} // namespace pcv::io
