#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace pointcloud {

struct Point3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    // Optional per-point normal from the PLY six-dimensional point record.
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
};

struct LoadResult {
    QVector<Point3D> points;
    QString error;
    bool ok = false;
    bool usedCache = false;
};

struct NoiseOptions {
    bool voxelEnabled = true;
    float voxelSize = 0.10f;
    bool statisticalEnabled = true;
    int meanK = 16;
    float stddevMultiplier = 1.5f;
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
struct ThreePointPlaneOptions { int neighborhoodSize=24; float distanceThreshold=0,normalAngleDegrees=20; bool preferHorizontal=true; int preferredAxis=3; };
struct ThreePointPlaneResult { QVector<Point3D> controlPoints,planePoints; PlaneModel model; float usedThreshold=0; QString error; bool ok=false; };

struct NoiseResult {
    QVector<Point3D> points;
    QString error;
    bool ok = false;
};

// Loads ASCII PLY and binary little/big-endian PLY vertex xyz data.
// RGB and other vertex properties are skipped safely.
bool loadPly(const QString &fileName, QVector<Point3D> &points,
             QString *error = nullptr);

// Loads a PLY through a validated binary cache next to the source file.
// The cache is created after the first successful PLY parse.
bool loadPlyCached(const QString &fileName, QVector<Point3D> &points,
                   QString *error = nullptr, bool *usedCache = nullptr);

LoadResult loadPlyCachedResult(const QString &fileName);

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
ThreePointPlaneResult selectPlaneFromThreeSeeds(const QVector<Point3D>&, const QVector<int>&, const ThreePointPlaneOptions& = {});

} // namespace pointcloud
