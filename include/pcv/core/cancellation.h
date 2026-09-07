#pragma once

#include <atomic>
#include <memory>

namespace pointcloud {

// Shared cancellation token used by long-running module pipelines.
using CancellationToken = std::shared_ptr<std::atomic_bool>;

} // namespace pointcloud
