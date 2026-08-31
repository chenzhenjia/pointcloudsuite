#pragma once

#include <QString>

namespace pcv::runtime {

void configureDataDirectory(const QString &path);
QString dataDirectory();
QString applicationDataDirectory();
QString cacheDirectory();
QString logDirectory();
QString exportDirectory();

} // namespace pcv::runtime
