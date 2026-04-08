# 简介
* 此仓库为 C++ 实现，大体改自 [rknpu2](https://github.com/rockchip-linux/rknpu2)，支持 YOLOv8 / YOLOv5 模型在 RK3588 上的高效推理
* 使用[线程池](https://github.com/senlinzhan/dpool) 异步操作 RKNN 模型，提高 RK3588 NPU 使用率
* 支持 **DeepSort 目标跟踪**（多目标持续跟踪 + 运动轨迹绘制）
* 通过 `classes.yaml` 灵活配置每个类别是否在画面上显示，无需改代码
* 使用 [osnet_x0_25](model/RK3588/osnet_x0_25_market.rknn) 作为 Re-ID 模型驱动 DeepSort

# 更新说明
* 新增 DeepSort 目标跟踪（第 4 个参数传入 Re-ID 模型路径即可启用）
* 新增 per-class 显示控制（`visible: true/false`），支持同一画面混合显示/隐藏不同类别
* 修复视频解码：使用 `LD_PRELOAD` 注入带 FFmpeg 支持的 OpenCV videoio 库
* 修复 YAML 解析：支持 `name:` 和 `visible:` 双字段，动态推断模型类别数
* 轨迹过滤：隐藏类别的目标不再绘制绿色轨迹线，但 DeepSort 跟踪逻辑仍正常执行

# 使用说明
### 快速运行

```bash
cd /home/orangepi/sheji/yolov8-rk3588-cpp-inference-main
bash run.sh
```

`run.sh` 已配置好所有环境变量，参数格式：

```bash
./build/rknn_yolov8_demo <yolov8模型> <视频源> <输出路径> <Re-ID模型>
```

| 参数 | 说明 | 示例 |
|------|------|------|
| 1 | YOLOv8 模型路径 | `install/model/RK3588/yolov8s.rknn` |
| 2 | 视频文件 / 摄像头序号 | `video/person.mp4` 或 `0` |
| 3 | RTSP 推流地址（可空） | `rtsp://ip:8554/stream` 或 `""` |
| 4 | Re-ID 模型路径（启用 DeepSort） | `model/RK3588/osnet_x0_25_market.rknn` |

### 示例

```bash
# 检测 + DeepSort 跟踪（推荐）
./build/rknn_yolov8_demo install/model/RK3588/yolov8s.rknn video/person.mp4 "" install/model/RK3588/osnet_x0_25_market.rknn

# 仅检测，不跟踪
./build/rknn_yolov8_demo install/model/RK3588/yolov8s.rknn video/person.mp4

# 摄像头 + 跟踪
./build/rknn_yolov8_demo install/model/RK3588/yolov8s.rknn 0 "" install/model/RK3588/osnet_x0_25_market.rknn
```

### per-class 显示控制

在模型同目录的 `classes.yaml` 中配置 `visible` 字段：

```yaml
classes:
  - name: person
    visible: true    # 显示
  - name: car
    visible: false   # 隐藏，但仍然跟踪
```

> 详细说明见 `运行说明.md`。

# 多线程模型帧率测试
* 使用performance.sh进行CPU/NPU定频尽量减少误差
* 测试模型来源: 
* [yolov5s-silu](https://github.com/rockchip-linux/rknn-toolkit2/tree/master/examples/onnx/yolov5) 
* [yolov5s-relu](https://github.com/rockchip-linux/rknpu2/tree/master/examples/rknn_yolov5_demo/model/RK3588)
* 测试视频可见于 [bilibili](https://www.bilibili.com/video/BV1zo4y1x7aE/?spm_id_from=333.999.0.0)

|  模型\线程数   | 1    |  2   | 3  |  4  | 5  | 6  | 12  |
|  ----  | ----  |  ----  | ----  |  ----  | ----  | ----  | ----  |
| Yolov5s - silu  | 15.9269  | 32.9192 | 52.8330  | 46.6782 | 58.2921 | 71.8070 |  |
| Yolov5s - relu  | 26.8601 | 58.0305 | 77.6904 | 80.7144 | 93.9126 | 101.1400 | 122.7334 |

# 补充
* 异常处理尚未完善, 目前仅支持rk3588/rk3588s下的运行
* relu版本相较于silu有着较大性能提升, 以及存在一些精度损失, 详情见于[rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo/tree/main/models/CV/object_detection/yolo)

# Acknowledgements
* https://github.com/rockchip-linux/rknpu2
* https://github.com/senlinzhan/dpool
* https://github.com/ultralytics/yolov5
* https://github.com/airockchip/rknn_model_zoo
* https://github.com/rockchip-linux/rknn-toolkit2
