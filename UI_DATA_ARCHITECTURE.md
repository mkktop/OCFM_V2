
# UI 数据更新架构方案

## 概述

本文档描述了 OCFM_V2 项目中 UI 层与数据层的分离架构，基于 LVGL v9 的观察者模式（Observer Pattern）实现。

---

## 架构设计

### 三层架构

```
┌─────────────────────────────────────────────────────────────┐
│  View 层 (UI)                                                 │
│  - lv_label, lv_chart 等控件                                  │
│  - 自动响应数据变化                                            │
└─────────────────────────────────────────────────────────────┘
                          ? (LVGL Observer 自动通知)
┌─────────────────────────────────────────────────────────────┐
│  Subject 层 (LVGL Subject)                                    │
│  - lv_subject_t (int/float/string/pointer/color)            │
│  - 数据与 UI 的桥梁                                            │
└─────────────────────────────────────────────────────────────┘
                          ? (手动同步)
┌─────────────────────────────────────────────────────────────┐
│  Model 层 (AppDataModel)                                      │
│  - 纯 C 结构体存储数据                                        │
│  - 独立于 UI，方便对接传感器/Modbus                            │
└─────────────────────────────────────────────────────────────┘
```

### 数据流向

```
LVGL定时器 (1000ms)
    ↓
app_model_update()  ← 从 RTC/传感器获取数据
    ↓
g_app_model (Model层)
    ↓
ui_manager-&gt;subjects (Subject层)
    ↓ (LVGL Observer 自动通知)
UI 控件 (View层)
```

---

## 文件结构

| 文件 | 说明 |
|------|------|
| `App/app_model.h` | 数据模型头文件 |
| `App/app_model.c` | 数据模型实现 |
| `App/ui/ui_conf.h` | UI 配置，包含 `ui_manager_t` 中的 subjects |
| `App/ui/ui.c` | UI 实现，包含 observer 回调、定时器、初始化和绑定 |

---

## 使用方法

### 1. 新增参数步骤（以温度为例）

#### 步骤 1：修改 Model 层 (`app_model.h`)

```c
typedef struct {
    // ... 现有字段 ...
    float temperature;  // 新增：温度
} AppDataModel;
```

#### 步骤 2：修改 ui_conf.h

```c
struct {
    // ... 现有字段 ...
    lv_subject_t temperature;  // 新增
} subjects;
```

#### 步骤 3：在 ui.c 中初始化 subject

在 `ui_create()` 函数中添加：

```c
lv_subject_init_float(&amp;ui_manager-&gt;subjects.temperature, 0.0f);
```

#### 步骤 4：在 ui.c 中同步数据

在 `ui_update_timer_cb()` 函数中添加：

```c
lv_subject_set_float(&amp;ui_manager-&gt;subjects.temperature, g_app_model.temperature);
```

#### 步骤 5：绑定到 UI 控件

在创建标签的地方添加：

```c
lv_subject_add_observer_obj(&amp;ui_manager-&gt;subjects.temperature, 
                             float_label_observer_cb, 
                             temp_label, 
                             NULL);
```

---

## 以后如何让 AI 新增参数的说辞

当你需要新增参数时，可以这样跟 AI 说：

### 模板话术

&gt; "请帮我在 UI 数据架构中新增一个参数：[参数名称]，类型是 [int/float/string]，用于显示在 [页面名称] 的 [控件位置]。"

### 示例 1：新增温度参数

&gt; "请帮我在 UI 数据架构中新增一个参数：temperature，类型是 float，用于显示在 create_details_tile 页面的底部温度标签位置。"

### 示例 2：新增瞬时流量参数

&gt; "请帮我在 UI 数据架构中新增一个参数：instant_flow，类型是 float，用于显示在 create_details_tile 页面的瞬时流量数据标签位置。"

### 示例 3：新增报警状态参数

&gt; "请帮我在 UI 数据架构中新增一个参数：alarm_status，类型是 string，用于显示在 create_details_tile 页面的顶部报警标签位置。"

---

## 当前已实现参数

| 参数 | 类型 | 说明 | 显示位置 |
|------|------|------|----------|
| `time_str` | string | 当前时间字符串 | `create_flow_record_tile` 顶部 |
| `record_time_sec` | uint32_t | 累计记录时间（秒） | - |
| `record_time_str` | string | 累计记录时间（格式化） | `create_flow_record_tile` 中间 |
| `total_flow` | double | 累计流量 | 预留 |

---

## Observer 回调函数

### string_label_observer_cb

通用的字符串类型标签观察者回调：

```c
static void string_label_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *label = lv_observer_get_target(observer);
    const char *text = lv_subject_get_string(subject);
    lv_label_set_text(label, text);
}
```

### 新增 float 类型回调（如需要）

```c
static void float_label_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *label = lv_observer_get_target(observer);
    float value = lv_subject_get_float(subject);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    lv_label_set_text(label, buf);
}
```

---

## 注意事项

1. **Subject 初始化顺序**：Subjects 必须在创建 UI 控件之前初始化
2. **缓冲区大小**：String 类型的 subject 需要分配足够的缓冲区
3. **线程安全**：LVGL Observer 在 LVGL 任务上下文中执行，线程安全
4. **数据更新频率**：定时器周期为 1000ms，可根据需要调整

---

## 扩展建议

1. **对接传感器数据**：在 `app_model_update()` 中从 Modbus 或传感器读取真实数据
2. **新增更多回调**：根据需要新增 int、float、color 等类型的 observer 回调
3. **批量更新**：可以创建 `app_model_sync_to_subjects()` 函数统一同步所有数据

