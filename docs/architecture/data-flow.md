# 数据流

1. PLY/深度帧 → 10读取 → 50位姿/手眼转换到 robot_base。
2. 整帧路径：30多层ICP或仅转换 → 原序合并 → 10事务输出。
3. 选面路径：60完整索引/单击或三点候选 → 应用预览确认 → 30所选平面拼接 → 10事务；已在robot_base的数据不再手眼转换。
4. 临时工件：60双平面/边缘/ROI/WObj1 → baseline/ROI/Mask/info；纹理严格依赖source_raster_index，扫描轨迹由60生成。
5. STEP：60/OCCT读取与采样 → 应用角色/修剪 → 60模板与10事务保存，坐标为cad_model/mm。
6. 相机：10 SDK帧/组帧/转换 → 应用预览与采集 → 原始帧事务保存；RAW converter核对SDK环境后输出PLY。

worker读取const Qt容器快照，返回私有结果；GUI线程发布。取消或revision/token/id/kind过期时丢弃结果，关闭先cancel/join。磁盘提交与GUI发布分开；已提交文件不因迟到取消被假装删除。
