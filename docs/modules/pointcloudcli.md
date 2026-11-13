# pointcloudcli：读取与临时工件命令行入口

版本 v0.5，核对日期2026-10-15。PCV_BUILD_CLI默认OFF；target使用C++17、Qt Core/Gui/Concurrent与10/50/60，不链接Widgets/OpenGL。根构建组合仍需P0核对，不把历史CLI-only配置当当前验证。[构建说明](../development/build-windows.md) · [契约](../contracts/data_contracts_v0.5.md) · [历史](../archive/history_pre_v0.5.md)。

## 命令与输入输出

| command | behavior |
|---|---|
| --help / --version | 用法 / pointcloudcli PROJECT_VERSION |
| inspect ply file.ply [--json] | 只读Header |
| read ply file.ply [--json] | 完整读取并输出格式/点数/Bounds/intensity摘要，不写新文件 |
| temp-workpiece run job.json [--data-dir path] [--allow-overwrite] | Job解析、50转换、两组三点、WObj1/ROI/Mask和四件套 |

Job schema为pointcloudcli-temp-workpiece-job-v1，kind=temp_workpiece_job；相对路径以Job目录解析，拒绝穿越，控制点索引为转换后robot_base空间。CLI不是交互选面/实时相机或拼接入口；registration_diagnostic仍独立。

成功摘要stdout，阶段进度/业务失败JSON写stderr，基础用法错误输出错误文本与用法。退出码0/2/10/20/30/40/130分别成功/契约/输入/转换/几何工件/输出/取消。--data-dir只对业务命令有效，顺序为CLI、PCV_DATA_DIR、exe旁data、LOCALAPPDATA/PointCloudSuite/data的首个可写路径。

## 验证与部署

CLI和Tests均开启时应用CMake注册15项契约，覆盖参数、ASCII/LE/BE读取、摘要/边界、四件套、ROI、覆盖与穿越拒绝；当前全工程组合本轮未运行。便携部署由脚本-CliOnly Release -Deploy控制，清空已有目录前需明确授权，详见构建文档。

现有无安装器/签名、无CLI写出转换或拼接、无实时相机、40/90/100功能。历史创建/部署记录集中到归档，不复制测试通过声明。
