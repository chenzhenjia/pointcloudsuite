#pragma once

#include <QtGlobal>
#include <QString>

namespace pcv::view {

struct ProcessMemoryUsage {
    bool available = false;
    quint64 workingSetBytes = 0;
    quint64 privateCommitBytes = 0;
};

ProcessMemoryUsage readProcessMemoryUsage();

QString formatMemoryBytes(quint64 bytes);

QString formatProcessMemoryUsage(const ProcessMemoryUsage &usage);

} // namespace pcv::view
