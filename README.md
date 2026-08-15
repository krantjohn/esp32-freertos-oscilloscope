# ESP32-S3 FreeRTOS Mini Oscilloscope (简易数字示波器) 📈

基于 **ESP32-S3** 与 **FreeRTOS** 实现的双任务、双缓冲简易数字示波器项目。本项目通过硬件定时器精准控制采样率，利用二值信号量实现中断与任务同步，借助双缓冲与队列机制实现零拷贝波形传递与流畅 OLED 刷新。

---

## ✨ 核心特性

- **⏱️ 硬件定时器精准采样**：配置 ESP32 硬件定时器（80 分频，计时间隔 100µs），以 **10 kSPS** 稳定频率触发中断，避免软件延迟带来的采样抖动。
- **🔄 FreeRTOS 双缓冲机制 (Double Buffering)**：采用 `Buffer A / Buffer B` 双缓冲轮换，采样任务与数据消费/渲染任务完全解耦，从根源上杜绝数据竞争与画面撕裂。
- **⚡ 边沿触发与自动触发 (Edge & Auto Trigger)**：
  - 支持上升沿电平触发（默认阈值 2048 / 1.65V）；
  - 配备 50ms 超时自动触发机制（Auto Trigger），在无有效触发信号时依然保持波形流畅刷新。
- **📊 实时波形参数自动测量**：
  - 最大电压 $V_{\max}$、最小电压 $V_{\min}$、峰峰值 $V_{\text{pp}}$ 计算；
  - 基于多周期上升沿跨度平均算法，精准计算波形频率 $F$。
- **📺 高效 OLED 界面渲染**：基于 `U8g2` 图形库驱动 128×64 SSD1306 OLED 屏幕，实时绘制连续折线波形与底部参数栏。
- **🧪 内置测试信号源**：利用 ESP32-S3 LEDC 外设在 `GPIO 2` 生成 759Hz 占空比 50% 的 PWM 方波，无需外部信号发生器即可快速自测闭环。

---

## 🛠️ 硬件与引脚分配 (Pinout)

| 引脚功能 | ESP32-S3 引脚 | 说明 |
| :--- | :--- | :--- |
| **ADC 采样输入** | `GPIO 1` | 模拟信号输入通道（0 ~ 3.3V） |
| **测试方波输出** | `GPIO 2` | 内置 759Hz PWM 信号（可直接短接 GPIO 1 自测） |
| **OLED SDA** | `GPIO 9` | I2C 数据线 |
| **OLED SCL** | `GPIO 8` | I2C 时钟线 |
| **OLED VCC / GND** | `3V3 / GND` | 供电引脚 |

> ⚠️ **注意**：ESP32-S3 ADC 输入电压范围为 0 ~ 3.3V，请勿输入超过 3.3V 的高压信号，以免损坏芯片引脚。

---

## 🏗️ 软件架构设计

```mermaid
flowchart TD
    subgraph ISR ["硬件定时器中断 (10 kHz)"]
        Timer[hw_timer_t 中断触发] -->|xSemaphoreGiveFromISR| Sem[二值信号量 sampleSemaphore]
    end

    subgraph SamplingTask ["FreeRTOS 采样任务 (Priority 5)"]
        Sem -->|xSemaphoreTake| WaitSem[等待定时触发]
        WaitSem --> ReadADC[读取 ADC_PIN (GPIO 1)]
        ReadADC --> TriggerCheck{上升沿触发 / 50ms 超时}
        TriggerCheck --> FillBuf[填充 128 点到 captureBuffer]
        FillBuf --> SwapBuf[双缓冲交换 capture / ready]
        SwapBuf -->|xQueueSend| Queue[波形队列 waveformQueue]
    end

    subgraph MainLoop ["主任务 (Core 1)"]
        Queue -->|xQueueReceive| FetchWave[getWaveform 获取波形指针]
        FetchWave --> Measure[measureWaveform 计算 Vpp, Vmax, Freq]
        Measure --> Display[displayWaveform U8g2 绘制波形与参数]
    end
```

---

## 📁 代码目录结构

```text
ADC/
├── .gitignore              # Git 忽略配置文件
├── platformio.ini          # PlatformIO 编译环境与依赖配置
├── README.md               # 项目说明文档
├── LICENSE                 # MIT 开源协议
├── include/                # 头文件目录
└── src/
    ├── config.h            # 核心参数配置（引脚、采样率、触发电平等）
    ├── sampling.h/.cpp     # 定时器中断、FreeRTOS 采样任务、双缓冲队列
    ├── oscilloscope.h/.cpp # 波形特征分析（峰峰值、多周期频率测量）
    ├── display.h/.cpp      # U8g2 OLED 渲染与 UI 排版
    └── main.cpp            # 系统入口与主轮询
```

---

## 🚀 快速上手 (Getting Started)

### 1. 软件环境
- [VS Code](https://code.visualstudio.com/) + [PlatformIO IDE 插件](https://platformio.org/)

### 2. 编译与烧录
1. 使用 VS Code 打开本项目所在目录 `ADC`；
2. 确保 `platformio.ini` 配置正确：
   ```ini
   [env:esp32-s3-devkitc-1]
   platform = espressif32
   board = esp32-s3-devkitc-1
   framework = arduino
   lib_deps = 
       olikraus/U8g2@^2.36.5
   ```
3. 连接 ESP32-S3 开发板，点击 PlatformIO 状态栏的 **Build** (✓) 与 **Upload** (→) 进行编译并烧录。

### 3. 自测验证
- 用杜邦线将 **`GPIO 2`**（测试方波输出）连接到 **`GPIO 1`**（ADC 采样输入）；
- OLED 屏幕将清晰显示 759Hz 方波，底部参数实时显示 `F:759Hz Vpp:3.30 Vmax:3.30V`。

---

## 🔮 后续规划 (Roadmap)

- [ ] **DMA 连续采样**：升级至 ESP-IDF ADC Continuous (DMA) 驱动，采样率提升至 100kSPS+。
- [ ] **交互控制**：增加按键/旋钮编码器，支持时基调节、垂直灵敏度调节与触发模式（Auto/Normal/Single）切换。
- [ ] **频域分析**：引入 ESP-DSP FFT 算法，支持时域/频域频谱一键切换。
- [ ] **多通道采集**：支持双通道（CH1 / CH2）同步采集与李萨如图形显示。

---

## 📄 开源协议

本项目采用 [MIT License](LICENSE) 开源。
