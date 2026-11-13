#include "process_memory_usage_v1.h"

#include <QCoreApplication>

#include <iostream>

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << "FAILED line " << __LINE__ << ": " << #expression << '\n'; \
        return 1; \
    } \
} while (false)

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using pcv::view::formatMemoryBytes;
    using pcv::view::formatProcessMemoryUsage;

    CHECK(formatMemoryBytes(0) == QStringLiteral("0.0 MiB"));
    CHECK(formatMemoryBytes(1024ULL * 1024ULL) == QStringLiteral("1.0 MiB"));
    CHECK(formatMemoryBytes(1024ULL * 1024ULL * 1024ULL - 1) == QStringLiteral("1024.0 MiB"));
    CHECK(formatMemoryBytes(1024ULL * 1024ULL * 1024ULL) == QStringLiteral("1.00 GiB"));
    CHECK(formatMemoryBytes(5ULL * 1024ULL * 1024ULL * 1024ULL) == QStringLiteral("5.00 GiB"));

    pcv::view::ProcessMemoryUsage unavailable;
    unavailable.workingSetBytes = 1024ULL * 1024ULL;
    unavailable.privateCommitBytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;
    CHECK(formatProcessMemoryUsage(unavailable) == QStringLiteral("内存：不可用"));

    pcv::view::ProcessMemoryUsage synthetic;
    synthetic.available = true;
    synthetic.workingSetBytes = 200ULL * 1024ULL * 1024ULL;
    synthetic.privateCommitBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    CHECK(formatProcessMemoryUsage(synthetic) ==
          QStringLiteral("工作集 200.0 MiB | 私有提交 2.00 GiB"));
    synthetic.workingSetBytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;
    synthetic.privateCommitBytes = 200ULL * 1024ULL * 1024ULL;
    CHECK(formatProcessMemoryUsage(synthetic) ==
          QStringLiteral("工作集 5.00 GiB | 私有提交 200.0 MiB"));

    const auto actual = pcv::view::readProcessMemoryUsage();
#ifdef Q_OS_WIN
    CHECK(actual.available);
    CHECK(actual.workingSetBytes > 0);
    CHECK(actual.privateCommitBytes > 0);
#else
    CHECK(!actual.available);
#endif
    return 0;
}
