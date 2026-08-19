# ADC 硬件定时器直接驱动采样改造说明

## 1. 改造背景
在原版代码中：
- 硬件定时器设置周期为 100μs (10kHz)。
- 每次中断时仅通过 `xSemaphoreGiveFromISR` 释放信号量，唤醒 `samplingTask` 进行单点采样。
- **弊端**：FreeRTOS 任务调度在 ESP32 上存在微秒级的唤醒延迟和系统调度抖动（Jitter）。100μs 间隔本身很短，每次调度延迟会直接叠加在采样间隔上，导致采样时间不均匀，进而使频率计算（`result.frequency = SAMPLE_RATE / averagePeriod`）产生显著误差。

## 2. 核心改动点
1. **彻底移除 `samplingTask` 与 `sampleSemaphore`**：不再使用任务做逐点轮询与阻塞。
2. **在 `onTimer()` ISR 中实现状态机**：
   - `STATE_WAIT_TRIGGER`：硬件中断直接读取 ADC 并比对上升沿（或 50ms 自动超时）。
   - `STATE_COLLECTING`：严格以 100.000μs 的硬件时钟节拍填入 `captureBuffer`。
3. **安全双缓冲与帧级队列通知**：
   - 采满 128 点后，在 ISR 中交换双缓冲指针（`captureBuffer` 与 `readyBuffer`）。
   - 调用 `xQueueOverwriteFromISR` 将完整帧推入队列。
   - `main.cpp` 的 `loop()` 只需要在 `getWaveform()` 为 true 时处理一整帧数据，处理频率仅为数十赫兹，大幅减轻 CPU 负荷。

## 3. 文件清单
- [preview/sampling.h](file:///d:/720720/espproject/VScode/ADC/preview/sampling.h)：保持原有对外 API 完全一致。
- [preview/sampling.cpp](file:///d:/720720/espproject/VScode/ADC/preview/sampling.cpp)：硬件定时器直接驱动的采样实现。

## 4. 如何应用到源码
待您查阅满意后，只需将 `preview/sampling.cpp` 的内容覆盖到 `src/sampling.cpp` 即可。
