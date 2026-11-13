# input-output 使用指南

输入支持PLY、手眼XML、逐帧位姿、已保存相机深度帧/RAW、STEP和模板目录。JSON/PLY/PCVMAP/schema保持兼容，cad_model与robot_base不可混用。正式输出为新版本目录，位于源码树外，失败/取消不发布半成品。RAW工具：`depth_raw_to_ply_v1 --input <depth.json> --output <new.ply>`；须匹配原采集SDK环境。模板伴随文件、摘要、点序和来源映射不可手工拆改。

中文路径支持，坐标单位为mm。相机/机器人/真实STEP和大样本需要独立现场验收；软件测试不证明设备安全或机器人可执行性。

[数据契约](../contracts/data-contracts.md) · [文档索引](../README.md)
