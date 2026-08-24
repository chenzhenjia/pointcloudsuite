#pragma once

#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>
#include <QImage>
#include <QMatrix4x4>
#include <QVector3D>
#include <QtGlobal>
#include <functional>

namespace pointcloud {

using LoadProgressCallback = std::function<void(qsizetype loaded, qsizetype total)>;

struct LoadResult {
    QVector<Point3D> points;
    QString error;
    bool ok = false;
    bool usedCache = false;
    Point3D minimum;
    Point3D maximum;
    bool hasBounds = false;
    qint64 headerElapsedMs = 0;
    qint64 boundaryScanElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

struct NoiseOptions {
    bool voxelEnabled = true;
    float voxelSize = 0.10f;
    bool statisticalEnabled = true;
    int meanK = 45;
    float stddevMultiplier = 1.30f;
};

// One scan file and its fixed transform into the robot/world frame.  The
// first integration stage intentionally treats each PLY as a rigid snapshot;
// no per-point robot trajectory compensation is implied.
struct WorldCloudInput {
    QString filePath;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    QMatrix4x4 flangeFromDepth;
    enum class ScanProgressSource : quint8 { VertexOrder, LocalX, LocalY, LocalZ };
    ScanProgressSource scanProgressSource = ScanProgressSource::VertexOrder;
    enum class ScanDirection : quint8 { Auto, Forward, Reverse };
    // Forward maps the first scan sample to Start and the last to End.
    // Reverse maps the first sample to End and the last to Start. Auto uses
    // the world-axis displacement test implemented by the merge routine.
    ScanDirection scanDirection = ScanDirection::Auto;
    enum class ScanCoordinateMode : quint8 {
        Auto,
        // The PLY scan axis already contains the metric Start-to-End motion.
        // Apply one anchor flange pose to the complete local cloud.
        EmbeddedMotion,
        // The PLY contains per-profile camera coordinates; interpolate the
        // flange pose for every point using scan progress.
        PerPointPose
    };
    ScanCoordinateMode scanCoordinateMode = ScanCoordinateMode::Auto;
    bool voxelDownsample = true;
    float voxelSize = 0.25f;
    // Pure visual diagnostics keep the original camera coordinates and do not
    // apply the robot/hand-eye transform.
    bool applyRobotTransform = true;
};

struct IcpOptions {
    bool enabled = true;
    int maximumIterations = 30;
    float maximumCorrespondenceDistance = 2.0f;
    float convergenceTolerance = 0.001f;
    int maximumSamples = 120000;
    int minimumCorrespondences = 24;
    float maximumCorrectionTranslation = 100.0f;
    float maximumCorrectionAngleDegrees = 45.0f;
    // 2.5D reliability weights.  Robot-world coordinates remain the base;
    // these only affect correspondence selection and ICP fitting.
    bool useTwoPointFiveD = true;
    float zWeight = 0.25f;
    float edgeWeight = 0.20f;
    float depthEdgeThreshold = 0.70f;
    bool globalInitialization = true;
    bool useRobotInitialGuess = true;
    float fitnessThreshold = 0.90f;
    float rmseThreshold = 0.05f;
    float minimumOverlapRatio = 0.10f;
    float minimumUniqueReferenceRatio = 0.05f;
    // Robot-world XY overlap is checked before ICP.  A low value usually
    // indicates an incorrect Start/End pose, scan direction, hand-eye matrix,
    // or file order; do not let ICP manufacture a misleading correction.
    bool rejectIcpWhenLowWorldOverlap = true;
    float minimumWorldOverlapForIcp = 0.10f;
    bool useMultiScale = true;
    QVector<float> voxelLevels = {2.0f, 0.8f, 0.25f};
};

struct IcpDiagnostics {
    QString filePath;
    bool attempted = false;
    bool converged = false;
    bool accepted = false;
    bool usedGlobalInitialization = false;
    int iterations = 0;
    int correspondences = 0;
    int movingSampleCount = 0;
    float fitness = 0.0f;
    float rmse = 0.0f;
    float correctionTranslation = 0.0f;
    float correctionAngleDegrees = 0.0f;
    float overlapRatio = 0.0f;
    float uniqueReferenceRatio = 0.0f;
    float duplicateCorrespondenceRatio = 0.0f;
    float xyRmse = 0.0f;
    float zRmse = 0.0f;
    int completedLevels = 0;
    QString reason;
};

struct WorldCloudMergeResult {
    QVector<Point3D> points;
    QVector<int> cloudIds;
    QVector<qsizetype> sourceIndices;
    QVector<QString> sourceFiles;
    QString diagnostics;
    QVector<IcpDiagnostics> icpDiagnostics;
    qsizetype pointsBeforeCrossCloudFusion = 0;
    qsizetype crossCloudDuplicateCount = 0;
    float crossCloudFusionVoxelSize = 0.0f;
    struct OverlapDiagnostic {
        int cloudId = -1;
        float intersectionArea = 0.0f;
        float movingArea = 0.0f;
        float unionArea = 0.0f;
        float movingCoverage = 0.0f;
        float intersectionOverUnion = 0.0f;
        bool warning = false;
    };
    QVector<OverlapDiagnostic> overlapDiagnostics;
    QString error;
    bool ok = false;
};

struct GeometryFeatureOptions { int neighborCount = 24; float searchRadius = 0.0f; int minNeighbors = 3; bool useExistingNormals = true; int neighbors = 24; float radius = 0.0f; bool estimateNormals = true; };
struct GeometryFeature { float nx=0,ny=0,nz=0,curvature=0,linearity=0,planarity=0,scattering=0,lambda1=0,lambda2=0,lambda3=0; int neighborCount=0; bool valid=false; };
struct GeometryFeatureResult { QVector<GeometryFeature> features; QString error,summary; bool ok=false; };
struct FeatureOptions { int neighbors=24; float radius=0.0f; bool estimateNormals=true; int downsampleDenominator=1; };
struct FeatureResult { bool ok=false; QString error; qsizetype inputCount=0,sourceCount=0; QVector<float> curvature,linearity,planarity,sphericity,roughness; QString summary; };
struct CompletionOptions { bool completeCircles=true; float maxGapAngleDegrees=12.0f; int samplesPerGap=2; float radialTolerance=0.20f; int minCirclePoints=24; bool smoothEdges=true; };
struct CompletionResult { QVector<Point3D> points,generatedPoints; int generatedCount=0,circleCount=0; QString summary,error; bool ok=false; };
struct PlaneSegmentationOptions { float distanceThreshold=0.02f; int maxPlanes=10,iterations=600,minInliers=100,sampleDenominator=4; unsigned int randomSeed=1337u; bool preferHorizontal=true; float maxTiltDegrees=15.0f; QVector<quint8> edgeMask; };
struct PlaneModel { float a=0,b=0,c=1,d=0; int inlierCount=0; float meanDistance=0,maxDistance=0; };
enum class PointSourceType : quint8 { Measured, ParametricFill };
enum class FeatureReviewStatus : quint8 { Measurable, NeedsReview, Rejected };
struct FillPointMetadata { int featureId=-1; PointSourceType sourceType=PointSourceType::ParametricFill; float confidence=0,uncertainty=0; int pointIndex=-1; };
enum class ChamferProfileType : quint8 { Linear, Round };
struct ChamferOptions { float distanceThreshold=0.02f,minAngleDegrees=8,maxAngleDegrees=172,width=0,sampleSpacing=0; int minSupportPoints=12,maxCandidates=16; ChamferProfileType profileType=ChamferProfileType::Linear; };
struct ChamferCandidate { int planeA=-1,planeB=-1; PlaneModel model; Point3D lineOrigin,lineDirection; float angleDegrees=0,width=0,confidence=0,radius=0,planeRms=0,uncertaintyWidth=0,uncertaintyAngle=0; int supportCount=0; ChamferProfileType profileType=ChamferProfileType::Linear; FeatureReviewStatus reviewStatus=FeatureReviewStatus::NeedsReview; };
struct ChamferResult { QVector<Point3D> points,generatedPoints; QVector<FillPointMetadata> generatedMetadata; QVector<ChamferCandidate> candidates; int generatedCount=0; QString summary,error; bool ok=false; };
struct CircleDetectionOptions { float radialTolerance=0; int iterations=800,minInliers=16,angularBins=36,minOccupiedBins=10; unsigned int randomSeed=20260810u; };
struct CircleDetectionResult { Point3D center; float radius=0,rmsError=0; QVector<int> inlierIndices; int occupiedBins=0,angularBinCount=0; float angularCoverage=0,confidence=0,inlierRatio=0,coverageDegrees=0,uncertaintyRadius=0,uncertaintyCenter=0; FeatureReviewStatus reviewStatus=FeatureReviewStatus::Rejected; QString summary,error; bool ok=false; };
enum class CircleInteriorCleanupMode : quint8 { SurfaceLayerOnly, ClearProjection };
struct CircleInteriorCleanupOptions { float radius=0,edgeProtectionWidth=0,planeDistanceTolerance=0; CircleInteriorCleanupMode mode=CircleInteriorCleanupMode::SurfaceLayerOnly; };
struct CircleInteriorCleanupResult { QVector<Point3D> retainedPoints,removedPoints; QVector<int> retainedSourceIndices; float usedProtectionWidth=0; QString summary,error; bool ok=false; };
struct SimilarCircleSearchOptions { float radiusToleranceRatio=0.05f,minimumSimilarity=0.75f,minimumAngularCoverage=0.25f,radialTolerance=0; int angularBins=36,maximumCandidates=32,maximumBoundarySamples=8000; };
struct CircleCandidate { CircleDetectionResult circle; float similarity=0,angularCoverage=0,densitySimilarity=0; bool isReference=false; };
struct SimilarCircleSearchResult { QVector<CircleCandidate> candidates; QString summary,error; bool ok=false; };
struct PlaneSegmentationResult { QVector<int> labels; QVector<PlaneModel> planes; QString summary,error; bool ok=false; };
struct EdgePipelineOptions { GeometryFeatureOptions feature; float edgePercentile=0.90f,edgeCurvatureThreshold=0,edgeNeighborRadius=0; int edgeMinNeighbors=2; NoiseOptions denoise; PlaneSegmentationOptions planes; };
struct EdgePipelineResult { QVector<float> edgeScore; QVector<quint8> edgeMask; QVector<GeometryFeature> edgeFeatures; QVector<Point3D> filteredPoints; PlaneSegmentationResult planeResult; QString summary,error; bool ok=false; };
struct ThreePointPlaneOptions {
    float initialTolerance = 1.0f;
    float surfaceTolerance = 0.4f;
    float connectivityRadius = 0.0f;
    int ransacIterations = 300;
    int minInliers = 100;
    unsigned int randomSeed = 20260813u;
    bool keepSeedComponentOnly = true;
    bool useZAxisResidual = false;
    float maxNormalTiltDegrees = 45.0f;
    int pcaRefinementIterations = 2;
    int minimumDisconnectedComponentPoints = 30;
    float minimumDisconnectedComponentRatio = 0.005f;
    // Fast preview mode: fit the model but defer full-cloud classification and
    // connected-component traversal until the user confirms the candidate.
    bool deferFinalClassification = false;
};
struct PlaneEdgeOptions {
    float edgeGridSize = 0.2f;
    int morphologyCloseRadius = 1;
    int morphologyOpenRadius = 1;
    int maximumEdgeGridCells = 4000000;
    // 2D export scale.  Values above one supersample the plane image for
    // inspection/export without changing the physical grid size.
    int imageScale = 3;
    // Plane-image export frame. When enabled, UV=(0,0) is the requested
    // workpiece origin and the two vectors define the user-selected X/Y axes.
    bool useImageFrame = false;
    QVector3D imageOrigin;
    QVector3D imageAxisU;
    QVector3D imageAxisV;
    float imageCropWidth = 0.0f;
    float imageCropHeight = 0.0f;
    // Automatic export framing. When enabled, the valid workpiece bounds are
    // detected from mapped plane points, expanded on all four sides, rounded
    // up to a physical increment, and rasterized at a fixed pixel pitch.
    bool autoImageBounds = true;
    float imageMargin = 50.0f;
    float imagePixelSize = 0.05f;
    float imageRoundIncrement = 10.0f;
    qsizetype maximumImagePixels = 100000000;
    // Maximum signed distance from the fitted plane accepted by the mapping.
    // This is deliberately applied again at export time so stale or mixed
    // indices can never place an off-plane point in the PNG.
    float planeDistanceTolerance = 0.4f;
};
struct PlaneContour {
    QVector<Point3D> points;
    bool hole = false;
};
struct ThreePointPlaneResult {
    PlaneModel model;
    QVector<int> candidateIndices;
    QVector<int> planeIndices;
    QVector<int> disconnectedPlaneIndices;
    QVector<Point3D> controlPoints;
    QVector<Point3D> planePoints;
    float rmsError = 0.0f;
    float usedThreshold = 0.0f;
    float planarity = 0.0f;
    int pcaRefinementCount = 0;
    int connectedComponentCount = 0;
    int significantComponentCount = 0;
    bool deferred = false;
    QString error;
    bool ok = false;
};
struct PlaneEdgeResult {
    QVector<int> edgeIndices;
    QVector<PlaneContour> contours;
    QImage image;
    // Physical raster frame used to generate image. Image row 0 is the
    // maximum-V side; callers flip the row when mapping UV to top-left image.
    QVector3D origin;
    QVector3D axisU;
    QVector3D axisV;
    float minimumU = 0.0f;
    float minimumV = 0.0f;
    int gridWidth = 0;
    int gridHeight = 0;
    QString imageDirection = QStringLiteral("u_positive_right_v_positive_up");
    float gridSize = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int occupiedCellCount = 0;
    QString error;
    bool ok = false;
};
struct PlaneImageResult {
    QImage image;
    float gridSize = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int occupiedCellCount = 0;
    int inputIndexCount = 0;
    int mappedPlanePointCount = 0;
    int rejectedInvalidPointCount = 0;
    int rejectedNonPlanePointCount = 0;
    int rejectedOutsideRectangleCount = 0;
    float usedPlaneDistanceTolerance = 0.0f;
    QVector3D workpieceMinimum;
    QVector3D workpieceMaximum;
    bool usedWorkpieceFrame = false;
    bool automaticBounds = false;
    float pixelSize = 0.0f;
    float margin = 0.0f;
    float rawWidth = 0.0f;
    float rawHeight = 0.0f;
    bool edgeMask = false;
    QString error;
    bool ok = false;
};

struct WorkpieceCoordinateSystem {
    QVector3D originInRobotBase;
    QVector3D axisXInRobotBase;
    QVector3D axisYInRobotBase;
    QVector3D axisZInRobotBase;
    QMatrix4x4 workpieceToRobotBase;
    QMatrix4x4 robotBaseToWorkpiece;
    float poseA = 0.0f;
    float poseB = 0.0f;
    float poseC = 0.0f;
    float orthogonalityError = 0.0f;
    bool valid = false;
    QString error;
};

struct PlanePlyExportResult {
    qsizetype exportedPointCount = 0;
    QString filePath;
    QString error;
    bool ok = false;
};

struct NoiseResult {
    QVector<Point3D> points;
    QString error;
    bool ok = false;
};

// Loads ASCII PLY and binary little/big-endian PLY vertex xyz data.
// RGB and other vertex properties are skipped safely.
bool loadPly(const QString &fileName, QVector<Point3D> &points,
             QString *error = nullptr,
             const LoadProgressCallback &progress = {});

// Loads a PLY through a validated binary cache next to the source file.
// The cache is created after the first successful PLY parse.
bool loadPlyCached(const QString &fileName, QVector<Point3D> &points,
                   QString *error = nullptr, bool *usedCache = nullptr);

LoadResult loadPlyCachedResult(const QString &fileName);
// Direct asynchronous-load entry point.  It reads the source PLY without
// creating or consulting an intermediate cache file.
LoadResult loadPlyResult(const QString &fileName);
LoadResult loadPlyResultWithProgress(const QString &fileName,
                                     const LoadProgressCallback &progress);

// Loads each PLY once, applies its supplied rigid world transform, and
// concatenates the measured points.  No virtual/centroid points are created.
WorldCloudMergeResult mergePlyCloudsInWorld(const QVector<WorldCloudInput> &inputs,
                                             const IcpOptions &icp = {});

// HiViewer-style proportional thinning. denominator=1 keeps all points,
// denominator=2 keeps approximately 1/2, denominator=16 keeps approximately 1/16.
QVector<Point3D> proportionalDownsample(const QVector<Point3D> &points,
                                         int denominator);

// Spatial octree LOD: returns one representative point per occupied octree
// leaf, bounded by targetCount. The input order is preserved only loosely.
QVector<Point3D> octreeLod(const QVector<Point3D> &points, qsizetype targetCount);

NoiseResult removeNoise(const QVector<Point3D> &points, const NoiseOptions &options);

GeometryFeatureResult extractGeometryFeatures(const QVector<Point3D>&, const GeometryFeatureOptions& = {});
FeatureResult extractFeatures(const QVector<Point3D>&, const FeatureOptions& = {});
CompletionResult completeGeometry(const QVector<Point3D>&, const GeometryFeatureResult*, const CompletionOptions& = {});
ChamferResult completeChamfers(const QVector<Point3D>&, const ChamferOptions& = {});
CircleDetectionResult detectCircleOnPlane(const QVector<Point3D>&, const PlaneModel&, const CircleDetectionOptions& = {});
CircleInteriorCleanupResult cleanCircleInterior(const QVector<Point3D>&, const PlaneModel&, const CircleDetectionResult&, const CircleInteriorCleanupOptions& = {});
SimilarCircleSearchResult findSimilarCirclesOnPlane(const QVector<Point3D>&, const PlaneModel&, const CircleDetectionResult&, const SimilarCircleSearchOptions& = {});
PlaneSegmentationResult segmentPlanes(const QVector<Point3D>&, const PlaneSegmentationOptions& = {});
EdgePipelineResult runEdgeAwarePipeline(const QVector<Point3D>&, const EdgePipelineOptions& = {});
ThreePointPlaneResult extractPlaneFromThreePoints(const QVector<Point3D>&, const QVector<int>&, const ThreePointPlaneOptions& = {});
ThreePointPlaneResult selectPlaneFromThreeSeeds(const QVector<Point3D>&, const QVector<int>&, const ThreePointPlaneOptions& = {});
WorkpieceCoordinateSystem buildWorkpieceCoordinateSystem(
    const QVector<Point3D> &, const QVector<int> &, int originIndex = -1,
    bool preferPositiveZ = true);
WorkpieceCoordinateSystem buildWorkpieceCoordinateSystem(
    const QVector<Point3D> &, const QVector3D &origin,
    const QVector3D &planeNormal,
    int xAxisPointIndex, int yAxisPointIndex,
    bool preferPositiveZ = false);
PlaneEdgeResult segmentPlaneEdges(const QVector<Point3D>&, const QVector<int>&,
                                  const PlaneModel&, const PlaneEdgeOptions& = {});
// Projects the selected plane from the current display cache into a 2D image
// without changing edge states.  The image is suitable for preview/export
// and can be passed to a later, independent edge-processing stage.
PlaneImageResult extractPlaneImage(const QVector<Point3D>&, const QVector<int>&,
                                   const PlaneModel&, const PlaneEdgeOptions& = {});
PlaneImageResult rasterizePlaneEdgeMask(const QVector<Point3D>&,
                                        const QVector<int>&,
                                        const PlaneModel&,
                                        const PlaneEdgeOptions&,
                                        const PlaneEdgeResult&);
PlanePlyExportResult exportPlanePly(const QString &fileName,
                                    const QVector<Point3D> &points,
                                    const QVector<int> &planeIndices,
                                    const PlaneModel &model,
                                    const WorkpieceCoordinateSystem &frame,
                                    const QString &sourceFile = {});
} // namespace pointcloud
