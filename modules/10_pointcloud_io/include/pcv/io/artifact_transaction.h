#pragma once

#include <pcv/core/point_types.h>

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace pcv::io {

class ArtifactTransaction
{
public:
    ArtifactTransaction(QString outputRoot,
                        bool allowOverwrite = false,
                        std::function<bool()> isCancelled = {});
    ~ArtifactTransaction();

    ArtifactTransaction(const ArtifactTransaction &) = delete;
    ArtifactTransaction &operator=(const ArtifactTransaction &) = delete;

    bool stageBinaryPly(const QString &relativePath,
                        const QVector<pointcloud::Point3D> &points,
                        bool includeNormals = false,
                        const QStringList &comments = {});
    bool stageGrayscalePng(const QString &relativePath, const QImage &image);
    bool stageJson(const QString &relativePath, const QByteArray &json);
    bool stageBytes(const QString &relativePath, const QByteArray &bytes);

    bool commit();
    void abort();

    QString outputRoot() const { return m_outputRoot; }
    QString error() const { return m_error; }
    bool committed() const { return m_committed; }

private:
    bool validateRelativePath(const QString &relativePath, QString *absolutePath) const;
    bool cancelled();
    bool ensureStagingDirectory();
    bool writeFile(const QString &path, const QByteArray &bytes);
    bool rollbackCommitted(const QVector<QString> &destinations,
                           const QVector<QString> &backups);

    QString m_outputRoot;
    QString m_stagingRoot;
    bool m_allowOverwrite = false;
    std::function<bool()> m_isCancelled;
    QString m_error;
    QStringList m_stagedRelativePaths;
    bool m_committed = false;
};

} // namespace pcv::io
