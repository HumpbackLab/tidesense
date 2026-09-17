# SchurVINS 在 TideSense / Cortex-A53 上的优化评估

当前实板 CPU 热点及同步 SD 日志干扰的纠正见 [热点复测](profile/HOTSPOTS.md)。

本文记录优化前的分析；后续实现及开关见 [README.md](README.md)，实测结果见 [bench/RESULTS.md](bench/RESULTS.md)。

日期：2026-09-17。依据当前源码、GCC 11 静态构建产物、`SchurVINS/hotpoint.md` 和本 SDK 的 IVE 用户态/内核实现。本次仅分析，没有修改算法，也没有测得优化后的板端加速比。

## 1. 热点表可以提供方向，但不能直接当作当前基线

- 表中是 FAST-9；当前 `scripts/schurvins/CMakeLists.txt` 没有编译 FAST-9 NEON 源文件，也没有定义 `HAVE_FAST_NEON`。`svo_direct/src/feature_detection_utils.cpp:145` 实际走标量 FAST-10；当前 ELF 的符号也与此一致。
- 表中提到了基线和 NEON 两版，却只给出基线占比，没有两版耗时、cycles 总量、二进制标识、分辨率及跟踪结果。有效采样数的变化不能直接换算为加速比。
- 这些是 period 加权的 self 占比。极线搜索调用的评分等成本不能再归到它的 self 上；内联后的评分又可能显示在 `updateZMSSD` 符号中。
- `cycles:u` 不覆盖内核执行和硬件等待。引入 IVE 后用户态 cycles 下降，不能单独证明每帧延迟下降。

表中检测、评分、NMS 合计 37.91%，是最值得重新确认的一组。如果这个占比适用于当前版本，整组耗时减半，整机理论加速约为 `1/(1-0.3791+0.3791/2)=1.234`，即吞吐提高约 23.4%、耗时减少约 19.0%。这是条件估算，不是实测预测。

## 2. 已确认的 SIMD 接入问题

当前 GCC 11 在 `-mcpu=cortex-a53` 下定义 `__ARM_NEON`，不定义 `__ARM_NEON__`。源码中的 halfSample、patch score 和部分 alignment 检查后者，因此这些手写 NEON 分支未启用。

这不表示整个程序没有 NEON：Eigen 支持这两种宏，OpenCV 也有独立的 SIMD 路径，其他标量循环可能被编译器自动向量化。不过，对当前 ELF 中 `vk::halfSample` 的反汇编检查确认，它确实是 `ldrb/add/asr/strb` 的逐像素循环，没有向量化。

不能全局简单增加 `-D__ARM_NEON__`：这还会切换到语义不同的旧版 `align2D_NEON`。建议逐个内核接入，使用项目自己的能力宏和独立开关。

## 3. 推荐的实施顺序

| 顺序 | 项目 | 方案 | 主要风险/验证 |
|---|---|---|---|
| 0 | 重建当前版本基线 | 同一真实双目+IMU replay，记录 ELF、配置、每阶段耗时及结果 | 旧表的 FAST-9 与当前 FAST-10 不一致 |
| 1 | halfSample | 连续像素 NEON，保留精确四像素求和后除 4 | stride、ROI、奇数尺寸、尾部及取整 |
| 1 | ZMSSD | 8×8 patch 的整数 SIMD 累加 | 逐候选评分完全一致、累加位宽 |
| 2 | FAST-10 检测和评分 | 保持 FAST-10 语义的 NEON；减少无效区域工作 | 不能直接混用 FAST-9 检测与 FAST-10 评分 |
| 2 | NMS / 临时内存 | 复用缓冲，评估按行评分缓冲 | 同分抑制规则、候选顺序、格子边界 |
| 3 | align2D | 对现有亮度模型的插值、残差、累加向量化 | 收敛、gain/offset 参数、浮点误差 |
| 3 | warpAffine | 坐标计算简化、连续计算与离散取数分开 | 随机读、floor 边界、插值截断 |
| 4 | IVE 金字塔 | 大图层 Resize，小图层 NEON，实测切换点 | 缓存维护、提交等待、缩放采样规则 |
| 5 | IVE 角点替换 | 单独评估 Shi–Tomasi 算法版本 | 特征分布和 VIO 精度发生变化 |

这里的实施顺序兼顾风险和验证成本；FAST 组可能收益最大，但 halfSample 和 ZMSSD 更适合作为首批可精确比对的修改。

### halfSample

位置：`SchurVINS/vikit/vikit_common/src/vision.cpp:47`、`:73`。

已有 NEON 用交错载入处理相邻 2×2 像素，适合作为基础。需要补齐非 16 整除宽度的尾部，以及奇数高度处理：旧分支只检查宽度，内部按输入高度每次加 2，奇数高度存在越界风险。不能采用两次带舍入的平均代替四像素总和右移 2，否则像素值可能不同。

多层金字塔的宽度未必一直是 16 的倍数，例如 752→376→188。只恢复原来的分支开关，仍会错过部分层。复用各层内存，只构建消费者需要的层。

### ZMSSD

位置：`SchurVINS/svo_direct/include/svo/direct/patch_score.h:56`、`:228`，调用方 `svo_direct/src/matcher.cpp:292`。

已有 8×8 评分 NEON 路径很适合 A53。另一个明确问题是构造函数中的 `if(patch_size_ == 0)`：实际 8×8 patch 不可能进入该 NEON 分支。

模板的 sumA/sumAA 已经缓存，不需要重复实现这项优化。重点是候选 patch 的 sumB、sumBB、sumAB，用足够宽的整数累加并保持原公式的除法规则。按行取 8 字节适合图像 stride，无需为了评分复制整个候选 patch。

### FAST 与 NMS

位置：`SchurVINS/svo_direct/src/feature_detection_utils.cpp:145`；依赖源码在 `install/schurvins-build/deps/fast_neon/src/`。

现有 `HAVE_FAST_NEON` 分支使用 FAST-9，但后面无条件调用 FAST-10 scorer。仅打开这个分支会同时改变检测定义，并使评分器输入不再保证满足 FAST-10 条件。优先做同语义 FAST-10 SIMD；若选择 FAST-9，检测与评分必须配套，作为单独算法变体验证。

可以沿水平方向一次处理多个中心像素，对圆周各偏移做连续载入；评分复用检测阶段的数据是否划算，需要权衡寄存器压力和额外存储。OpenCV 默认的 FAST-9 也不是无差别替换。

当前先对整层检测、评分、NMS，再跳过已经占用的格子。按需要补点的格子减少工作可能比单纯 SIMD 更有价值，但必须保留 NMS 邻居的评分以及 FAST 半径 3 的图像读取范围，不能简单裁掉相邻格子。提前达到特征数量就停止扫描，也会改变每格最大分数的选择结果。

现有 NMS 利用按行排序的稀疏点和行索引，并非普通全点两两比较。先复用 vector/行缓冲；是否改成密集评分图取决于角点密度。保留 `>=` 同分抑制规则及输出顺序。

### align2D

位置：`SchurVINS/svo_direct/src/feature_alignment.cpp:212`、`:571`。

标量实现支持 x/y 位移、亮度 offset/gain，构造 4 维系统，通过参数控制亮度估计；旧 NEON 实现是 x/y/平均亮度差的 3 维模型，使用定点插值，也不接收相同的 gain/offset 和迭代记录参数。它们不是可以无条件互换的两个实现。

建议保留现有模型，向量化每行插值、梯度与残差乘积、Hessian/Jᵀr 累加。小矩阵求解继续在 CPU。参考梯度和 Hessian 当前已在迭代外计算，不能把这项当作尚未实现的收益。

### warpAffine 和极线搜索

位置：`SchurVINS/svo_direct/src/patch_warp.cpp:112`，`svo_direct/src/matcher.cpp:340` 起。

warpAffine 处理的是小 patch，并非整幅图像。矩阵求逆已经在像素循环外。可评估每行起点和列偏移复用、整 patch 安全边界快路径；连续坐标计算适合 SIMD，变换后的图像取数较离散，NEON 没有通用 gather，收益通常比 halfSample 难确定。坐标递推可能改变浮点误差和 floor 结果，应逐像素比较。

极线扫描已经跳过连续重复的整数像素，不必再重复加这项。缩短搜索范围、调整种子数量/特征数量可减少工作，但属于精度与耗时的权衡。不要仅凭其 2.13% self 占比判断整个搜索便宜。

## 4. SG2002 IVE：适合哪些，不适合哪些

本 SDK 实际硬件路径是 `cvi_mpi/modules/ive/src/cvi_ive.c` → `osdrv/interdrv/ive/common/cvi_ive_interface.c` → `osdrv/interdrv/ive/hal/cv181x/cvi_ive_platform.c`。根目录另一个 `ive/` 目录的 README 描述 TPU/MLIR 相关实现，不能与此硬件路径混为一谈。

官方 [CV180x/CV181x IVE API 文档](https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/MPI/IVE_API_Reference/build/IVEAPIReference_en.pdf) 说明 Resize 支持双线性和面积插值。以下可用性判断进一步核对了当前 SDK 源码，仍不等于已经完成当前板卡的运行验证。

### 首选实验：金字塔大图层 Resize

`cvi_ive_platform.c:8934` 的 Resize 有实际硬件配置和提交过程。2 倍缩小可先试 AREA，但必须核对取整和采样相位是否等价于现有 halfSample；从 L0 直接缩到 L2 也不保证等价于连续两次缩小。

使用预分配物理缓冲，正确处理 stride、CPU 写后 flush、设备写后 CPU 读前 invalidate。普通 `cv::Mat` 指针不能直接当物理地址。若能复用相机输出的兼容灰度/Y 缓冲，可减少复制；具体需要核对采集格式和缓冲所有权，不能预先承诺零拷贝。

按层测量完整的 `复制 + 缓存维护 + 提交 + 等待 + 结果访问`。小层很可能更适合 NEON，由测试确定切换点。硬件异步与 CPU 工作重叠有价值，但后续层依赖前一层，不能假设所有金字塔层同时执行。

特别注意：当前 `cvi_ive.c:2554` 的 Resize 包装调用 ioctl 后直接返回 0，没有传播 ioctl 的返回值。API 返回成功不足以证明执行成功；实验必须检查内核错误和输出内容，必要时先修复错误传播。

### 次选实验：Shi–Tomasi 角点

`cvi_ive_platform.c:9470` 的 STCandiCorner 有实际实现，涉及特征值计算和候选图生成。其算法、阈值和排序与 FAST 不同，CPU 仍需把候选图转换为 SchurVINS 的特征及网格选择。

当前驱动要求额外工作区至少 `4 × height × stride + sizeof(IVE_ST_MAX_EIG_S)`。例如 stride=1280、height=800，仅该主体就是 4,096,000 字节，尚未计入输入输出；双目需要复用工作区或考虑额外内存。这是算法替换实验，优先级低于保持原有结果的优化。

### 不建议首先迁移的部分

- 8×8 ZMSSD：SAD、NCC 与零均值平方差不是同一评分；离散小 patch 的提交和搬运可能超过计算时间。
- align2D：需要与现有亮度模型、迭代策略一致的支持；当前公共 API 未提供可以直接替换它的实现。
- patch warpAffine：小 patch 的离散取样不适合逐块硬件提交，当前公共 API 也未发现直接等价接口。
- ioctl 头中的 LKOpticalFlowPyr/STCorner 名称不能证明可用；当前检查的公共 API、ioctl 分发和 cv181x HAL 没找到对应完整调用路径。不能按其他 Sophon 芯片的能力推断本芯片。

## 5. 怎么验证收益

1. 固定真实双目+IMU 数据、校准、配置、帧范围，记录源码版本、ELF SHA/build-id、GCC 参数、输入尺寸、CPU 频率和温度。区分稳态跟踪、初始化、重定位、补点阶段。
2. 用匹配的未剥离 ELF 解析 perf；同时记录每双目帧墙钟时间、CPU 时间、P50/P95、丢帧/积压、各阶段调用次数。不要只比较函数百分比。统计预先规定，不因结果筛选区间。
3. halfSample/ZMSSD/同语义 FAST/NMS 先做结果一致性检查：边界、stride、ROI、奇数尺寸、非向量整倍数宽度、同分角点。不要只测常用整齐尺寸。
4. align2D/warp/IVE Resize 记录像素或匹配差异、迭代数、成功率；在真实序列检查跟踪点数、跟踪丢失、ATE/RPE（有真值时）。六帧合成 smoke test 不足以证明 VIO 精度。
5. 每次只改一个方向，多轮交错运行基线/候选，最终对组合版本重测；各项加速比不能直接相加。
6. IVE 同时量 CPU 节省和端到端延迟，确认硬件确实写出正确结果，避免只看到 `cycles:u` 消失。先做独立 Resize 实验，再接流水线。

暂不优先全局 `-ffast-math`、把后端 double 全改 float、盲目加线程或迁移到 TPU。当前证据首先指向图像前端；单 A53 上增加计算线程不产生额外 CPU 算力。跨翻译单元优化/LTO 可作为后续独立构建实验，但应在稳定基线和热点内核优化之后评估。
