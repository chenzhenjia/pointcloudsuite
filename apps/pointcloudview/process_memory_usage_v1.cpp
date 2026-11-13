#include "process_memory_usage_v1.h"

#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>
#endif

namespace pcv::view {
namespace {
constexpr quint64 kBytesPerMiB = 1024ULL * 1024ULL;
constexpr quint64 kBytesPerGiB = 1024ULL * kBytesPerMiB;
}

ProcessMemoryUsage readProcessMemoryUsage() {
    ProcessMemoryUsage usage;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters))) {
        return usage;
    }
    usage.available = true;
    usage.workingSetBytes = static_cast<quint64>(counters.WorkingSetSize);
    usage.privateCommitBytes = static_cast<quint64>(counters.PrivateUsage);
#endif
    return usage;
}

QString formatMemoryBytes(quint64 bytes) {
    if (bytes >= kBytesPerGiB)
        return QStringLiteral("%1 GiB").arg(
            QString::number(static_cast<double>(bytes) / static_cast<double>(kBytesPerGiB), 'f', 2));
    return QStringLiteral("%1 MiB").arg(
        QString::number(static_cast<double>(bytes) / static_cast<double>(kBytesPerMiB), 'f', 1));
}

QString formatProcessMemoryUsage(const ProcessMemoryUsage &usage) {
    if (!usage.available)
        return QStringLiteral("内存：不可用");
    return QStringLiteral("工作集 %1 | 私有提交 %2")
        .arg(formatMemoryBytes(usage.workingSetBytes),
             formatMemoryBytes(usage.privateCommitBytes));
}

} // namespace pcv::view
