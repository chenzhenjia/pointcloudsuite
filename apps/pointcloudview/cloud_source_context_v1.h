#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

enum class CloudCoordinateFrameV1 { Unknown, RobotBase, CadModel, Camera };

// Application provenance belongs to the actual buffer, not the plane/WObj1 session.
struct CloudSourceContextV1 {
    CloudCoordinateFrameV1 coordinateFrame = CloudCoordinateFrameV1::Unknown;
    QJsonObject cad;
    QString entry = QStringLiteral("unknown");
    QStringList files;
    QString manifest;
    QJsonArray processingSteps;

    QJsonObject templateSource() const
    {
        QJsonObject source{{QStringLiteral("entry"), entry},
            {QStringLiteral("files"), QJsonArray::fromStringList(files)},
            {QStringLiteral("manifest"), manifest},
            {QStringLiteral("processing_steps"), processingSteps}};
        if (!cad.isEmpty()) source.insert(QStringLiteral("cad"), cad);
        return source;
    }
};
