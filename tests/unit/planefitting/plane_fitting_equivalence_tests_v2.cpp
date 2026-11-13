#include <pcv/planefitting/plane_fitting.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

using pcv::planefitting::Options;
using pcv::planefitting::Result;
using pointcloud::Point3D;

// Serialize every pre-existing field, including normal coordinates and float bits.
// Timings and the newly added cancellation flag are intentionally excluded.
QByteArray completeResult(const Result &r)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << r.model.a << r.model.b << r.model.c << r.model.d << r.model.inlierCount
           << r.model.meanDistance << r.model.maxDistance << r.candidateIndices
           << r.planeIndices << r.disconnectedPlaneIndices;
    for (const auto *points : {&r.controlPoints, &r.planePoints}) {
        stream << qint64(points->size());
        for (const auto &p : *points) stream << p.x << p.y << p.z << p.nx << p.ny << p.nz;
    }
    stream << r.rmsError << r.usedThreshold << r.planarity << r.pcaRefinementCount
           << r.connectedComponentCount << r.significantComponentCount << r.deferred
           << r.error << r.ok;
    return bytes;
}

struct Fixture {
    const char *name;
    QVector<Point3D> points;
    QVector<int> seeds{0, 39, 1560};
    Options options;
    bool success = true;
};

// Captured from the untouched implementation on MSVC 19.51 / Qt 6.8.3,
// independently in Debug and Release (both produce these same complete hashes).
const char *baselineHashes[] = {
    "2dc2f1eb7ca379aac174ee7a489d0b6e03bc73d40184c550f4ad243d19104cad",
    "c6a85459edffdd8e1a8e85dc28b969337fe34923f4b56609e58d4300fcfa7153",
    "ddddaeb15ba8825ea0e7ae60b882ed0ec1bc2fb13958847d38be32cf9e52c6b7",
    "4c7176d4187b7c4afccb9dc4246ca3ae31e83d54c61a35dd66d875c77d69ca7e",
    "d93c670241252cfb02949fe708b274b7d6f45ddf7c5d5bc9503c03c37c505831",
    "8eab4cd981ccd100b2d485c5f53ef748b97a6979c04f3b5df314082bddc405cf",
    "5c8fc89f859bdc7b2ba62504a22221c55307c149ed8671dc27f2cf9191e48bc6",
    "2a185108eef6d0c52b67ba0e50575a78a5ab69611cd4cdbeab37fe53a03238f8",
    "c2b046d4ef24db4ed54c8f4cf18edfbf16a2175ebf1f1d44aaefbf9ef53edffd",
    "e5b5b3a472092a5c07b51b9d8efa79908c71e6fe462d5239fd483b2d32e0844c",
    "8d426fb9b7e900febd1da94a341bea55983bb82fd882be52acdfb53227a02760",
    "1f8ca2b954be32886987a09afabdf1899ef3f277f4565aaeeddc2a7ec28e1b63",
    "5f18f2593de895e0ba170a455ce3ad5460dcf70b3800b34871ae406668ddf7c5",
    "4782a61e0b73c7dc8a5c0728fd54074b4e773c2676e63bd6824fc95b860b4559"
};

QVector<Fixture> fixtures()
{
    QVector<Fixture> cases;
    Fixture dense{"dense"};
    dense.options.minInliers = 3;
    dense.options.connectivityRadius = 1.01f;
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x)
        dense.points.push_back({float(x), float(y), 0, 0.1f, 0.2f, 0.3f});
    cases.push_back(dense);
    Fixture duplicates = dense;
    duplicates.name = "duplicates";
    for (int i = 0; i < 12000; ++i) duplicates.points.push_back(dense.points.at(i % 1600));
    cases.push_back(duplicates);
    Fixture uneven = dense;
    uneven.name = "uneven";
    for (int i = 0; i < 10000; ++i) uneven.points.push_back({float(i % 100) * 0.001f, float(i / 100) * 0.001f, 0});
    cases.push_back(uneven);
    Fixture disconnected = dense;
    disconnected.name = "disconnected";
    for (const auto &p : dense.points) disconnected.points.push_back({p.x + 100, p.y, p.z});
    for (int i = 0; i < 5; ++i) disconnected.points.push_back({200.f + i, 0, 0});
    cases.push_back(disconnected);
    Fixture cross = disconnected;
    cross.name = "cross_seeds";
    cross.seeds = {0, 39, 3160};
    cross.success = false;
    cases.push_back(cross);
    Fixture all = disconnected;
    all.name = "all_components";
    all.options.keepSeedComponentOnly = false;
    all.options.connectivityRadius = 0; // Automatic radius and projection bounds.
    cases.push_back(all);
    Fixture boundary = dense;
    boundary.name = "boundary";
    boundary.options.connectivityRadius = 1.0f;
    boundary.points.push_back({40, 39, 0}); // Exactly one radius: connected.
    boundary.points.push_back({41.00001f, 39, 0}); // Just outside: disconnected.
    cases.push_back(boundary);
    Fixture noisy = dense;
    noisy.name = "noisy_tilted";
    for (int i = 0; i < noisy.points.size(); ++i) {
        auto &p = noisy.points[i];
        p.z = 0.2f * p.x - 0.1f * p.y + 3.f + float(i % 7 - 3) * 0.01f;
    }
    for (int i = 0; i < 100; ++i) noisy.points.push_back({float(i % 40), float(i / 40), 10});
    noisy.options.useZAxisResidual = true;
    cases.push_back(noisy);
    Fixture vertical = dense;
    vertical.name = "vertical";
    for (auto &p : vertical.points) { p.z = p.x; p.x = 2.f; }
    cases.push_back(vertical);
    Fixture deferred = noisy;
    deferred.name = "deferred";
    deferred.options.deferFinalClassification = true;
    cases.push_back(deferred);
    Fixture invalid = dense;
    invalid.name = "invalid_index"; invalid.seeds = {0, 39, 99999}; invalid.success = false;
    cases.push_back(invalid);
    invalid.name = "duplicate_seed"; invalid.seeds = {0, 39, 39};
    cases.push_back(invalid);
    invalid.name = "collinear"; invalid.seeds = {0, 1, 2};
    cases.push_back(invalid);
    invalid.name = "nonfinite"; invalid.seeds = dense.seeds;
    invalid.points[39].z = std::numeric_limits<float>::infinity();
    cases.push_back(invalid);
    return cases;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto cases = fixtures();
    int fixtureIndex = 0;
    for (const auto &fixture : cases) {
        // Keep multiple Qt shared references alive throughout all worker executions.
        const QVector<Point3D> points = fixture.points;
        const QVector<Point3D> extraCopy = points;
        const QVector<int> seeds = fixture.seeds;
        const QVector<int> extraSeeds = seeds;
        const auto serial = pcv::planefitting::fit(points, seeds, fixture.options);
        if (serial.ok != fixture.success) { std::cerr << fixture.name << " unexpected status\n"; return 1; }
        const auto expected = completeResult(serial);
#ifdef PCV_GENERATE_BASELINE
        std::cout << fixture.name << " " << QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toHex().constData() << '\n';
#else
#if defined(_MSC_VER) && _MSC_VER == 1951 && QT_VERSION == QT_VERSION_CHECK(6, 8, 3)
        if (QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toHex()
            != baselineHashes[fixtureIndex]) {
            std::cerr << fixture.name << " original baseline mismatch\n"; return 1;
        }
#endif
        for (int tasks : {1, 2, 4}) {
            Options options = fixture.options;
            options.parallelEnabled = true;
            options.maxParallelTasks = tasks;
            const auto parallel = pcv::planefitting::fit(points, seeds, options);
            if (completeResult(parallel) != expected) {
                std::cerr << fixture.name << " mismatch tasks=" << tasks << '\n'; return 1;
            }
        }
#endif
        ++fixtureIndex;
    }
#ifndef PCV_GENERATE_BASELINE
    const Options defaults;
    if (defaults.parallelEnabled || defaults.maxParallelTasks != 4) return 1;
    const auto fixture = cases.at(2);
    for (int tasks : {1, 2, 4}) {
        for (int stopAfter : {0, 12, 200, 1500, 6000, 14000}) {
            std::atomic_int calls{0};
            Options options = fixture.options;
            options.parallelEnabled = true;
            options.maxParallelTasks = tasks;
            options.isCancelled = [&calls, stopAfter]() { return calls.fetch_add(1) >= stopAfter; };
            const auto r = pcv::planefitting::fit(fixture.points, fixture.seeds, options);
            if (!r.cancelled || r.ok || r.error != QStringLiteral("cancelled")
                || !r.planeIndices.isEmpty() || !r.candidateIndices.isEmpty()
                || !r.disconnectedPlaneIndices.isEmpty() || !r.planePoints.isEmpty()) {
                std::cerr << "cancel failure tasks=" << tasks << " stop=" << stopAfter << '\n'; return 1;
            }
        }
    }
    // Simulate owner shutdown: request cancellation, join fit, then release captured data.
    // Callback count must stop when fit returns: no nested worker can outlive its owner.
    std::atomic_bool stop{false};
    std::atomic_int calls{0};
    Options options = fixture.options;
    options.parallelEnabled = true;
    options.isCancelled = [&stop, &calls]() { ++calls; return stop.load(); };
    auto running = std::async(std::launch::async, [&fixture, &options]() {
        return pcv::planefitting::fit(fixture.points, fixture.seeds, options);
    });
    while (calls.load() < 100) {
        if (running.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) return 1;
        std::this_thread::yield();
    }
    stop.store(true);
    if (running.wait_for(std::chrono::seconds(5)) != std::future_status::ready) return 1;
    if (!running.get().cancelled) return 1;
    const int joinedCalls = calls.load();
    if (calls.load() != joinedCalls) return 1;
    std::cout << "plane_fitting_equivalence_tests_v2: PASS\n";
#endif
}
