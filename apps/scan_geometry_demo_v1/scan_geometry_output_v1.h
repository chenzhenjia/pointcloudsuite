#pragma once
#include "scan_geometry_contract_v1.h"

#include <pcv/io/artifact_transaction.h>
#include <pcv/roi/automatic_workpiece_frame_v1.h>
#include <pcv/stitching/selected_plane_stitch.h>

#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

namespace pcv::demo {

struct GeometryFrameOutputV1 {
    quint64 id = 0;
    QString sourceFile;
    QString scanJson;
    QVector<int> interferenceIndices;
};

struct GeometryOutputRequestV1 {
    ScanGeometryContractV1 contract;
    QVector<GeometryContractFrameV1> contractFrames;
    pcv::stitching::SelectedPlaneStitchResult result;
    QSet<int> removed;
    pcv::roi::WorkpieceCoordinateSystem wobj1;
    int referenceFrame = -1;
    quint64 resultRevision = 0;
    QVector<GeometryFrameOutputV1> frames;
    QString outputRoot;
    std::function<bool()> isCancelled;
};

struct GeometryOutputResultV1 {
    bool success = false;
    bool cancelled = false;
    QString error;
    QString outputDirectory;
    QString trimmedPly;
    QString trimmedMapping;
    QString sidecar;
    QString contractFile;
};

GeometryOutputResultV1 saveSelectedPlaneGeometryV1(const GeometryOutputRequestV1 &request);

} // namespace pcv::demo
