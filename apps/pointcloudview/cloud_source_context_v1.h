#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Application provenance belongs to the actual buffer, not the plane/WObj1 session.
struct CloudSourceContextV1 {
    bool robotBaseConfirmed = false;
    QString entry = QStringLiteral("unknown");
    QStringList files;
    QString manifest;
    QJsonArray processingSteps;

    QJsonObject templateSource() const
    {
        return {{QStringLiteral("entry"), entry},
            {QStringLiteral("files"), QJsonArray::fromStringList(files)},
            {QStringLiteral("manifest"), manifest},
            {QStringLiteral("processing_steps"), processingSteps}};
    }
};
