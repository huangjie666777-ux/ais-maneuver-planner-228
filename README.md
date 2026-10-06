# ais_maneuver228港区AIS后端

项目仓库：https://github.com/huangjie666777-ux/ais-maneuver-planner-228

C++20 + Boost.Asio/Beast 1.74 实现的港区 AIS 会遇预警服务（无前端）。UDP 接收
`!AIVDM` 语句，按 MMSI 合并船名与动态状态，HTTP 查询船舶列表和本船对各目标的
会遇预测（DCPA/TCPA）。

## 构建

```bash
./prepare-deps.sh   # 已备可跳过：解压 Boost 1.74、下载 nlohmann/json
make -j             # build/ais_maneuver228 与 build/ais_maneuver228_tests
make test
```

要求 g++ 11.4（C++20）。Boost.Asio/Beast 按头文件方式使用，仅链接 pthread。

## 启动

```bash
./build/ais_maneuver228 \
  --udp 127.0.0.1:10110 \
  --http 127.0.0.1:8080 \
  --own-mmsi 477000001 \
  --origin 22.12,114.01 \
  --valid-seconds 60 \
  --predict-seconds 600 \
  --guard-meters 500
```

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--udp H:P` | `127.0.0.1:10110` | 环回 UDP 监听地址 |
| `--http H:P` | `127.0.0.1:8080` | HTTP 监听地址 |
| `--own-mmsi` | 必填 | 本船 MMSI |
| `--origin lat,lon` | `0,0` | 港区原点；原点纬度绝对值必须 < 70° |
| `--valid-seconds` | `30` | 动态报告有效期 |
| `--predict-seconds` | `300` | 预测区间上限（秒） |
| `--guard-meters` | `1000` | 警戒距离（米） |

SIGINT/SIGTERM 取消异步操作、关闭 socket 后退出。

## AIS 处理

- 每条报文必须是 `!AIVDM` 语句；校验 `*HH` 异或校验和、6 个字段、序号/频道/
  填充位（0–5）以及六位装码字符表（ASCII 88–95 在装码中被跳过）。
- 仅处理消息类型 1/2/3（动态）与 5（船名）；其他类型计入 `ignored` 后丢弃。
- 动态字段：SOG 1023、COG 3600、经纬度哨兵 91°/181° 均视为不可用并显式标缺测；
  缺测位置不沿用旧坐标。有符号经纬度按二补码解码（1/10000 分），SOG 单位
  0.1 节，COG 单位 0.1°（自北顺时针）。
- 多片语句按“发送端点 + 序号 + 频道”隔离；支持乱序到达、重复幂等；同组总片数
  或重复片内容冲突时整组丢弃；未集齐的组 5 秒后清理。多片必须带序号。系统对
  缓存片数、组数和船数均有上限。
- 报告年龄只使用本机单调收包时刻（`steady_clock`），不使用 AIS 秒字段排序。

## 坐标与预测

以港区原点为中心做等距圆柱投影（球半径 6371000 m）：

- 东向 `E = R · Δlon · cos(lat0)`，北向 `N = R · Δlat`；
- 经差先取 ±180° 最短方向；
- 仅船位距原点 50 km 内的报告参与预警。

速度换算为 `节 × 1852/3600` m/s，航向自北顺时针（东向分量 `v·sin COG`，
北向分量 `v·cos COG`）。各船按各自年龄从最后收包位置**匀速直线**外推到同一查询
时刻，再计算相对运动：

- 相对位置 `r`、相对速度 `v`；
- 最小相对距离 `DCPA = min|r + v·t|`，时间夹在 `[0, predict_seconds]`；
- 相对速度为零时 TCPA 取 0 秒；`DCPA ≤ 警戒米数` 时 `alarm=true`。

注意：匀速、局部平面均为短时间近似，不考虑转向、加减速、海图障碍与大地曲率
高阶项。缺测、过期、超出港区或本船无效时目标状态为 `indeterminate`，
`safe=false`，绝不当作安全。

## HTTP 接口

- `GET /health`：存活检查。
- `GET /v1/ships`：全部已知船舶的最新状态（含缺测/过期标记、东北米坐标）。
- `GET /v1/own`：本船状态与收包计数。
- `GET /v1/encounters`：本船对各目标的会遇评估，含 `dcpa_m`、`tcpa_seconds`、
  `alarm`、`safe`；无法评估时给出 `reason`（过期、缺测、超区等）。

## UDP 样例

`samples/ais_send.py` 只用 Python 标准库生成带校验和的语句：

```bash
# 单船动态 + 船名
./samples/ais_send.py --udp 127.0.0.1:10110 \
  --mmsi 477000001 --name 'HARBOR ONE' \
  --lat 22.123456 --lon 114.012345 --sog 12.3 --cog 45

# 对遇场景：本船 477000001 与目标 477000002
./samples/ais_send.py --scenario closing

curl -s http://127.0.0.1:8080/v1/ships | jq .
curl -s http://127.0.0.1:8080/v1/encounters | jq .
```

`build/ais_maneuver228_tests` 覆盖校验和、字段、六位装码、1/2/3 与 5 解码、乱序/重复/冲突
分片以及 DCPA/TCPA 几何。

## 代码结构

- `src/ais.*`：语句校验、六位载荷、消息解码、分片重组。
- `src/track.*`：加锁的 MMSI 航迹表、投影、龄期与清理。
- `src/predict.*`：匀速外推与相对运动。
- `src/udp_listener.*`：异步 UDP 接入。
- `src/http_server.*`：Beast 异步查询接口。
- `src/main.cpp` / `src/config.cpp`：装配、信号与参数。
