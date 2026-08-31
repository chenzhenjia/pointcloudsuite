#include <pcv/render/pointcloud_canvas_contract.h>

namespace pcv::render {

bool validateSnapshot(const RenderSnapshot &snapshot, QString *error)
{
    if (snapshot.states.size() != 0 && snapshot.states.size() != snapshot.points.size()) {
        if (error) *error = QStringLiteral("渲染点状态与点云数量不一致");
        return false;
    }
    if (snapshot.revision == 0 && !snapshot.points.isEmpty()) {
        if (error) *error = QStringLiteral("非空渲染快照必须包含有效版本号");
        return false;
    }
    return true;
}

} // namespace pcv::render
