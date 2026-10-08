#include <pcv/render/pointcloud_canvas_contract.h>

#include <cmath>

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

bool validatePointColorOverlay(const PointColorOverlay &overlay, qsizetype pointCount,
                               QString *error)
{
    if (pointCount < 0 || overlay.colors.size() != pointCount
        || overlay.valid.size() != pointCount) {
        if (error) *error = QStringLiteral("点颜色覆盖与点云数量不一致");
        return false;
    }
    for (int i = 0; i < overlay.colors.size(); ++i) {
        const QVector4D &color = overlay.colors.at(i);
        if (!std::isfinite(color.x()) || !std::isfinite(color.y())
            || !std::isfinite(color.z()) || !std::isfinite(color.w())
            || color.x() < 0.0f || color.x() > 1.0f
            || color.y() < 0.0f || color.y() > 1.0f
            || color.z() < 0.0f || color.z() > 1.0f
            || color.w() < 0.0f || color.w() > 1.0f) {
            if (error) *error = QStringLiteral("点颜色覆盖包含无效颜色值");
            return false;
        }
        if (overlay.valid.at(i) > 1) {
            if (error) *error = QStringLiteral("点颜色覆盖有效标记无效");
            return false;
        }
    }
    return true;
}

bool validateCoordinateFrame(const CoordinateFrame &frame, QString *error)
{
    if (!frame.valid) return true;
    const QVector<QVector3D> vectors = {frame.originInRobotBase,
                                        frame.axisXInRobotBase,
                                        frame.axisYInRobotBase,
                                        frame.axisZInRobotBase};
    for (const QVector3D &vector : vectors) {
        if (!std::isfinite(vector.x()) || !std::isfinite(vector.y())
            || !std::isfinite(vector.z())) {
            if (error) *error = QStringLiteral("渲染坐标系包含非有限数值");
            return false;
        }
    }
    return true;
}

bool validateContours(const QVector<Contour> &contours, QString *error)
{
    for (const Contour &contour : contours) {
        for (const QVector3D &point : contour.points) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())
                || !std::isfinite(point.z())) {
                if (error) *error = QStringLiteral("渲染轮廓包含非有限数值");
                return false;
            }
        }
    }
    return true;
}

} // namespace pcv::render
