#pragma once
#include "scan_geometry_contract_v1.h"

#include <pcv/io/artifact_transaction.h>
#include <pcv/roi/automatic_workpiece_frame_v1.h>
#include <pcv/stitching/selected_plane_stitch.h>

#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

namespace pcv::demo {

// 原输入顺序的帧证据；包含跳过帧，id不是数组下标。
struct GeometryFrameOutputV1 {
    quint64 id = 0;
    QString sourceFile;
    QString scanJson; // 可空；原扫描JSON路径。
    QVector<int> interferenceIndices; // 原帧完整点下标，不是拼接/显示点下标。
};

// 同步保存的只读请求；点/映射为完整robot_base/mm结果，调用期间不可并发修改。
struct GeometryOutputRequestV1 {
    ScanGeometryContractV1 contract; // 可选；启用时正式目录由runtimeRoot及业务身份确定。
    QVector<GeometryContractFrameV1> contractFrames; // 契约启用时提供，按原输入顺序。
    pcv::stitching::SelectedPlaneStitchResult result;
    QSet<int> removed; // result.points原始下标；越界使保存失败。
    pcv::roi::WorkpieceCoordinateSystem wobj1; // 必须valid；robot_base/mm，角度deg。
    int referenceFrame = -1; // frames/contractFrames原输入0-based序号。
    quint64 resultRevision = 0; // 记录来源版本；此函数不访问/比对活动会话。
    QVector<GeometryFrameOutputV1> frames;
    QString outputRoot; // 非契约模式在此创建唯一geometry目录；应为源码树外绝对路径。
    std::function<bool()> isCancelled; // 可空；worker轮询，回调必须线程安全。
};

struct GeometryOutputResultV1 {
    bool success = false; // 仅transaction.commit成功后true，不代表严格质量通过。
    bool cancelled = false; // 已观察到取消；success=false时区分取消与失败。
    QString error; // 失败/取消原因；不得从字符串解析稳定错误码。
    QString outputDirectory; // 以下路径仅success时有效；不枚举全部产物。
    QString trimmedPly;
    QString trimmedMapping;
    QString sidecar;
    QString contractFile; // 仅契约启用并提交成功时非空。
};

// 同步IO，不发送会话信号；失败/取消撤销本事务，已有拼接事务独立保留。
GeometryOutputResultV1 saveSelectedPlaneGeometryV1(const GeometryOutputRequestV1 &request);

} // namespace pcv::demo
