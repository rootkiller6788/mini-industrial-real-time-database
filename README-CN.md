# Mini Industrial Real-Time Database（迷你工业实时数据库）

**从零开始、零依赖的 C 语言实现**，涵盖工业实时数据库与历史数据归档组件，参照 AVEVA/OSIsoft PI System 架构设计。每个模块对应大学数据库、分布式系统、数据压缩与工业自动化相关课程，将工业数据基础设施概念转化为可运行的 C 代码，实现理论与实践的桥接。

## 子模块

| 子模块 | 主题 | 参考课程 |
|--------|--------|-------------|
| [mini-historian-data-retrieval-sql](mini-historian-data-retrieval-sql/) | 时序聚合计算、SQL 检索、插值、窗口函数、死区压缩 | MIT 6.830, CMU 15-721 |
| [mini-pi-asset-analytics-af-analytics](mini-pi-asset-analytics-af-analytics/) | AF 分析引擎、事件帧触发、表达式解析器（词法/AST）、KPI 汇总、资产层次结构、EMA/Holt-Winters 平滑 | MIT 15.071, MIT 6.035 |
| [mini-pi-asset-framework-af](mini-pi-asset-framework-af/) | AFElement、AFAttribute、AFTemplate、数据引用管道、枚举集、搜索 API | MIT 6.830, ISA-95 |
| [mini-pi-event-frames-notifications](mini-pi-event-frames-notifications/) | 事件帧时间索引存储、相关性分析、触发器评估引擎、事件模板、通知投递 | MIT 6.824, Stanford CS347 |
| [mini-pi-integrator-opcua-mqtt](mini-pi-integrator-opcua-mqtt/) | PI Integrator 管道/调度/健康监控、OPC UA 网桥、MQTT 数据流、数据模型 | MIT 6.824, Stanford CS244E |
| [mini-pi-system-osisoft-architecture](mini-pi-system-osisoft-architecture/) | PI Archive 归档、Snapshot 快照、Point Database 点数据库、Buffer 缓冲、Collective 高可用集群、Security 安全模型、System Management 系统管理 | MIT 6.824, MIT 6.033 |
| [mini-pi-vision-display-dashboard](mini-pi-vision-display-dashboard/) | 仪表盘网格布局、显示对象模型、ISA-101 HMI 合规、渲染管线、符号库、趋势可视化 | MIT 6.813, Stanford CS147 |
| [mini-time-series-compression-algorithms](mini-time-series-compression-algorithms/) | 死区压缩、差分/二阶差分/Gorilla 编码、Huffman/RLE/算术熵编码、分段线性逼近、旋转门算法、DFT/DCT/小波变换 | MIT 6.441, Stanford EE376A |

## 设计理念

- **零外部依赖** — 纯 C（C99/C11），仅使用 `libc` 和 `libm`
- **模块自包含** — 每个目录自带 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **工业级模式** — 每个模块均参照真实的 AVEVA/OSIsoft PI System 组件与架构
- **理论到代码的映射** — 模块包含 `docs/` 目录，内有架构说明、算法参考文献及行业标准（ISA-95、ISA-101、ISO 22400）

## 构建方式

每个模块相互独立。进入模块目录后运行：

```bash
cd mini-historian-data-retrieval-sql
make all    # 构建全部
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-industrial-real-time-database/
├── mini-historian-data-retrieval-sql/           # 时序聚合、SQL 检索、插值、压缩
├── mini-pi-asset-analytics-af-analytics/        # AF 分析引擎、表达式解析、KPI 汇总
├── mini-pi-asset-framework-af/                  # AFElement、AFTemplate、数据引用管道
├── mini-pi-event-frames-notifications/          # 事件帧存储、相关性分析、通知投递
├── mini-pi-integrator-opcua-mqtt/               # Integrator 管道、OPC UA 网桥、MQTT 流
├── mini-pi-system-osisoft-architecture/         # PI Archive、Snapshot、Point DB、Buffer、HA、Security
├── mini-pi-vision-display-dashboard/            # 仪表盘布局、ISA-101 HMI、渲染、趋势
└── mini-time-series-compression-algorithms/     # 死区、差分编码、熵编码、PLA、变换压缩
```

## 许可证

MIT
