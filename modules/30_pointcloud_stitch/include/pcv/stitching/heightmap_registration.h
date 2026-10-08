#pragma once

#include <pcv/core/point_types.h>
#include <pcv/stitching/multiframe_registration.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include <functional>

namespace pointcloud {

// 2.5D height-map registration (controlled iteration).
//
// This header exposes the height-map construction, the trajectory diagnostic
// and the bounded coarse-to-fine height-residual search that feed the
// restricted point-to-plane ICP. The elevation is always the robot-base Z of a
// point projected on the robot-base X/Y plane; the source/target crops, the
// scanner order (scanRatios) and the robot poses are the existing 3D pipeline
// values, so no new hardware timestamp and no [X,Y,Z,A,B,C] coordinate
// semantic is introduced.
//
// The height-map path is additive: IcpOptions.heightMapRegistrationEnabled
// defaults to false and the legacy ICP pipeline keeps its behaviour and its
// formal output when the switch is off.

// Axis-aligned X/Y region of one height map, in robot base mm.
struct HeightMapRegion {
    float minimumX = 0.0f;
    float minimumY = 0.0f;
    float maximumX = 0.0f;
    float maximumY = 0.0f;
    bool valid() const {
        return std::isfinite(minimumX) && std::isfinite(minimumY)
            && std::isfinite(maximumX) && std::isfinite(maximumY)
            && maximumX > minimumX && maximumY > minimumY;
    }
};

struct HeightMapOptions {
    // The estimated grid resolution is clamped into this window.
    float minimumGridResolutionMm = 0.5f;
    float maximumGridResolutionMm = 2.0f;
    // Reported as the region support: a pair whose height map is thinner than
    // this is rejected with heightmap_insufficient_data.
    float minimumValidRatio = 0.25f;
    int minimumValidPoints = 100;
    float overlapMarginMm = 5.0f;
};

// One 2.5D elevation map. Every per-cell vector has width*height entries and is
// indexed by row * width + column; column grows with +X, row grows with +Y.
struct HeightMap {
    float gridResolutionMm = 0.0f;
    float originX = 0.0f;   // centre of cell (0, 0)
    float originY = 0.0f;
    int width = 0;
    int height = 0;
    // Local coordinate range actually covered by the map (robot base mm).
    QVector3D localMinimum;
    QVector3D localMaximum;
    // Bounds of the cloud that produced the map, without the overlap margin.
    // The search uses them to rebuild the candidate overlap box, so the score
    // region is always the intersection of the robot-initial overlap box and the
    // candidate overlap box, never the joint bounding box of both frames.
    QVector3D cloudMinimum;
    QVector3D cloudMaximum;
    // H(x,y) = z of the merged points of the cell; 0 on invalid cells.
    QVector<float> elevation;
    // H(x,y) after the robust trend plane was removed; 0 on invalid cells.
    QVector<float> detrended;
    // M(x,y): 1 when the cell received at least one point.
    QVector<quint8> mask;
    // Number of points merged into the cell.
    QVector<quint32> counts;
    // Source point index of the first point merged into the cell.
    QVector<qsizetype> sourceIndices;
    // Robust trend plane z = trendA * x + trendB * y + trendC.
    double trendA = 0.0;
    double trendB = 0.0;
    double trendC = 0.0;
    int trendIterations = 0;
    qsizetype validCellCount = 0;
    qsizetype validPointCount = 0;
    bool ok = false;
    // "heightmap_grid_resolution_invalid", "heightmap_insufficient_data",
    // "heightmap_not_heightfield" or "heightmap_region_invalid".
    QString error;

    float cellCenterX(int column) const {
        return originX + float(column) * gridResolutionMm;
    }
    float cellCenterY(int row) const {
        return originY + float(row) * gridResolutionMm;
    }
    int index(int column, int row) const { return row * width + column; }
    bool inside(int column, int row) const {
        return column >= 0 && row >= 0 && column < width && row < height;
    }
    bool valid(int column, int row) const {
        return inside(column, row) && mask.at(index(column, row)) != 0;
    }
    // Bilinear elevation sample of the detrended map at robot-base (x, y).
    // Neighbours with a zero interpolation weight are not required to be valid,
    // so an exact cell-centre offset reproduces the stored cell value. Returns
    // false when the sample would have to invent a value on a missing cell.
    bool sampleDetrended(float x, float y, float *value) const;
};

// Estimates the grid resolution from the median nearest-neighbour spacing of
// the cloud and clamps it into [minimum, maximum]. Returns false and sets
// error to "heightmap_grid_resolution_invalid" when no stable estimate exists.
bool estimateHeightMapGridResolution(const QVector<Point3D> &points,
                                     const HeightMapOptions &options,
                                     float *resolutionMm,
                                     QString *error);

// Builds one height map over region. Missing cells stay masked and never
// contribute a zero elevation to a score.
HeightMap buildHeightMap(const QVector<Point3D> &points,
                         const HeightMapRegion &region,
                         float gridResolutionMm,
                         const HeightMapOptions &options);

// Trajectory diagnostic of one adjacent pair. It only reads data the pipeline
// already has (scanRatios, the Start/End poses, the dominant travel axis and
// the point order); it never estimates a time offset and never adds a hardware
// timestamp.
struct HeightMapTrajectoryDiagnostic {
    bool ok = false;
    QString reason;
    bool scanRatiosMonotonic = false;
    bool scanRatiosInRange = false;
    int travelAxis = -1;
    bool travelAxisStable = false;
    float scanRatioDirection = 0.0f;
    float pointOrderJumpRatio = 0.0f;
    float displacementMm = 0.0f;
    float extentAlongTravelMm = 0.0f;
};

// Read-only pair input of the trajectory diagnostic. The pointers stay valid
// for the duration of the call and are never copied, so the diagnostic costs no
// frame-sized allocation.
struct HeightMapTrajectoryInput {
    const QVector<Point3D> *points = nullptr;
    const QVector<float> *scanRatios = nullptr;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    int dominantTravelAxis = -1;
    float signedTravel = 0.0f;
};

HeightMapTrajectoryDiagnostic diagnoseHeightMapTrajectory(const HeightMapTrajectoryInput &target,
                                                          const HeightMapTrajectoryInput &source);

struct HeightMapSearchOptions {
    bool searchEnabled = true;
    // Strict budget: what a candidate must respect to reach the formal output.
    float maximumTranslationMm = 10.0f;
    float maximumYawDegrees = 1.0f;
    // Recovery budget: retained for diagnostics, never applied to the formal
    // output of this iteration.
    float recoveryTranslationMm = 120.0f;
    float recoveryYawDegrees = 3.0f;
    float minimumRelativeImprovement = 0.01f;
    float gridResolutionMm = 1.0f;
    // Same margin the caller used when it built the two maps.
    float overlapMarginMm = 5.0f;
    std::function<bool()> isCancelled;
};

struct HeightMapSearchDiagnostic {
    bool attempted = false;
    bool accepted = false;
    QString reason;
    QMatrix4x4 candidate;
    float translationX = 0.0f;
    float translationY = 0.0f;
    float translationZ = 0.0f;
    float yawDegrees = 0.0f;
    float scoreBefore = 0.0f;
    float scoreAfter = 0.0f;
    float relativeImprovement = 0.0f;
    float correlationBefore = 0.0f;
    float correlationAfter = 0.0f;
    float sourceValidRatio = 0.0f;
    float targetValidRatio = 0.0f;
    int sourceValidPoints = 0;
    int targetValidPoints = 0;
    int candidateCount = 0;
    bool ambiguous = false;
    bool exceededStrictBudget = false;
};

// Bounded coarse-to-fine height-residual search. Stage I optimises dx/dy only,
// stage II a small dz/yaw window; the full 6 DoF is never opened and the scan
// trajectory is never used to guess an unobservable in-plane freedom.
//
// The score is the robust (trimmed) elevation residual over the intersection of
// the robot-initial overlap box and the candidate overlap box, measured on the
// valid mask only. Each candidate is compared against the robot initial value
// (zero offset) and must improve it by at least
// HeightMapSearchOptions::minimumRelativeImprovement.
HeightMapSearchDiagnostic searchHeightMapAlignment(const HeightMap &source,
                                                   const HeightMap &target,
                                                   const HeightMapSearchOptions &options);

} // namespace pointcloud
