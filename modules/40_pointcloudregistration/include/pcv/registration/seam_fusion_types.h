#pragma once

#include <QVector>
#include <QString>

#include <functional>

namespace pointcloud {

struct SeamFusionOptions {
    bool enabled = false;
    float halfWidth = 8.0f;
    float mutualDistance = 0.6f;
    float decisionCellSize = 0.5f;
    std::function<bool()> isCancelled;
};

struct SeamFusionDiagnostic {
    int cloudA = -1;
    int cloudB = -1;
    qsizetype bandPoints = 0;
    qsizetype bandPointsA = 0;
    qsizetype bandPointsB = 0;
    qsizetype mutualPairs = 0;
    qsizetype interpolatedPoints = 0;
    qsizetype unmatchedDiscarded = 0;
    qsizetype unmatchedPreserved = 0;
    qsizetype decisionCells = 0;
    qsizetype corePoints = 0;
    bool applied = false;
    bool actualOverlapValid = false;
    float projectedAMin = 0.0f;
    float projectedAMax = 0.0f;
    float projectedBMin = 0.0f;
    float projectedBMax = 0.0f;
    float actualOverlapMin = 0.0f;
    float actualOverlapMax = 0.0f;
    float seamProjection = 0.0f;
    QString reason;
};

struct SeamFusionResult {
    QVector<SeamFusionDiagnostic> diagnostics;
    qsizetype inputPoints = 0;
    qsizetype outputPoints = 0;
    QString error;
    bool ok = false;
    bool cancelled = false;
};

} // namespace pointcloud
