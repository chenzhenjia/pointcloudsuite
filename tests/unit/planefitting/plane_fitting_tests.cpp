#include <pcv/planefitting/plane_fitting.h>

#include <QCoreApplication>

#include <iostream>

namespace {
bool require(bool value, const char *message) { if (!value) std::cerr << message << '\n'; return value; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const pcv::planefitting::Options defaults;
    if (defaults.ransacIterations != 300 || defaults.pcaRefinementIterations != 2
        || defaults.randomSeed != 20260813u
        || defaults.minimumDisconnectedComponentPoints != 30) return 1;
    QVector<pointcloud::Point3D> points;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        points.push_back({float(x), float(y), 0.01f * float((x + y) % 2)});
    pcv::planefitting::Options options;
    options.initialTolerance = 1.0f;
    options.surfaceTolerance = 0.1f;
    options.minInliers = 3;
    const auto fitted = pcv::planefitting::fit(points, {0, 3, 12}, options);
    if (!require(fitted.ok && fitted.model.c > 0.99f && fitted.planeIndices.size() == points.size(),
                 "independent plane fitting should classify the horizontal cloud")) return 1;
    if (!require(!pcv::planefitting::fit(points, {0, 1}, options).ok,
                 "plane fitting must reject fewer than three controls")) return 1;
    if (!require(!pcv::planefitting::fit(points, {0, 1, 1}, options).ok,
                 "plane fitting must reject duplicate controls")) return 1;
    const auto center = pcv::planefitting::calculateBoundsCenter(points, {0, 3, 12});
    if (!require(center.ok && center.center == QVector3D(1.5f, 1.5f, 0.005f),
                 "bounds center should be calculated from finite indexed points")) return 1;
    if (!require(!pcv::planefitting::calculateBoundsCenter(points, {99}).ok,
                 "bounds center must reject out-of-range indices")) return 1;

    // A cloud with a non-finite control point is rejected by the fit.
    QVector<pointcloud::Point3D> nonFiniteCloud = points;
    nonFiniteCloud[7].z = std::numeric_limits<float>::quiet_NaN();
    if (!require(!pcv::planefitting::fit(nonFiniteCloud, {5, 6, 7}, options).ok,
                 "plane fitting must reject non-finite controls")) return 1;

    // Second-triplet consistency uses the same thresholds as both data-source
    // lines (JSON and plain PLY). A strictly planar cloud keeps the expected
    // angle and distance values exact.
    QVector<pointcloud::Point3D> flatCloud;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        flatCloud.push_back({float(x) * 5.0f, float(y) * 5.0f, 0.0f});
    pcv::planefitting::Options flatOptions = options;
    flatOptions.surfaceTolerance = 0.05f;
    const auto flatFit = pcv::planefitting::fit(flatCloud, {0, 3, 12}, flatOptions);
    if (!require(flatFit.ok, "the strictly planar cloud must be fitted")) return 1;
    const QVector<int> primary{0, 3, 12};
    const auto consistent = pcv::planefitting::validateConsistency(
        flatCloud, flatFit.model, primary, {5, 6, 9}, 1.0f, 0.4f);
    if (!require(consistent.passed
                 && consistent.status == pcv::planefitting::ConsistencyStatus::Passed,
                 "a coplanar second triplet must pass")) return 1;
    if (!require(consistent.normalAngleDegrees <= 1.0f
                 && consistent.maximumDistanceMm <= 0.4f,
                 "consistency diagnostics must stay inside the thresholds")) return 1;

    const auto reused = pcv::planefitting::validateConsistency(
        flatCloud, flatFit.model, primary, {5, 6, 12}, 1.0f, 0.4f);
    if (!require(reused.status == pcv::planefitting::ConsistencyStatus::ReusedPoint,
                 "the second triplet must not reuse primary points")) return 1;

    const auto collinear = pcv::planefitting::validateConsistency(
        flatCloud, flatFit.model, primary, {4, 5, 6}, 1.0f, 0.4f);
    if (!require(collinear.status == pcv::planefitting::ConsistencyStatus::Collinear,
                 "a collinear second triplet must be rejected")) return 1;

    const auto outOfRange = pcv::planefitting::validateConsistency(
        flatCloud, flatFit.model, primary, {5, 6, 99}, 1.0f, 0.4f);
    if (!require(outOfRange.status == pcv::planefitting::ConsistencyStatus::InvalidInput,
                 "out-of-range verification indices must be rejected")) return 1;

    QVector<pointcloud::Point3D> nonFiniteFlat = flatCloud;
    nonFiniteFlat[7].z = std::numeric_limits<float>::quiet_NaN();
    const auto nonFiniteTriplet = pcv::planefitting::validateConsistency(
        nonFiniteFlat, flatFit.model, primary, {5, 6, 7}, 1.0f, 0.4f);
    if (!require(nonFiniteTriplet.status == pcv::planefitting::ConsistencyStatus::InvalidInput,
                 "non-finite verification points must be rejected")) return 1;

    // A parallel but offset second triplet exceeds the distance threshold.
    QVector<pointcloud::Point3D> offsetCloud = flatCloud;
    for (int index : {5, 6, 9}) offsetCloud[index].z += 2.0f;
    const auto offset = pcv::planefitting::validateConsistency(
        offsetCloud, flatFit.model, primary, {5, 6, 9}, 1.0f, 0.4f);
    if (!require(offset.status == pcv::planefitting::ConsistencyStatus::DistanceExceeded
                 && offset.maximumDistanceMm > 0.4f,
                 "an offset second triplet must exceed the distance threshold")) return 1;

    // A second triplet on a perpendicular face exceeds the angle threshold.
    QVector<pointcloud::Point3D> mixedCloud = flatCloud;
    mixedCloud[5] = {5.0f, 0.0f, 0.0f};
    mixedCloud[6] = {5.0f, 1.0f, 0.0f};
    mixedCloud[9] = {5.0f, 0.0f, 1.0f};
    const auto tilted = pcv::planefitting::validateConsistency(
        mixedCloud, flatFit.model, primary, {5, 6, 9}, 1.0f, 0.4f);
    if (!require(tilted.status == pcv::planefitting::ConsistencyStatus::AngleExceeded
                 && tilted.normalAngleDegrees > 1.0f,
                 "a tilted second triplet must exceed the angle threshold")) return 1;

    // Cancellation stops the verification and is never reported as passed.
    const auto cancelled = pcv::planefitting::validateConsistency(
        flatCloud, flatFit.model, primary, {5, 6, 9}, 1.0f, 0.4f, []() { return true; });
    if (!require(!cancelled.passed && cancelled.cancelled
                 && cancelled.status != pcv::planefitting::ConsistencyStatus::Passed,
                 "a cancelled verification must not pass")) return 1;

    std::cout << "plane_fitting_tests: PASS\n";
    return 0;
}
