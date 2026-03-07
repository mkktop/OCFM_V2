# 1. 问题

fsmc\_st7789.c 文件中的 `fsmc_st7789_draw_pixel` 函数存在一个直接的函数调用错误，该函数调用了不存在的 `lcd_set_window` 函数，而实际应该调用的是 `fsmc_st7789_set_window` 函数。

## 1.1. **函数调用错误**

在 `Middlewares/lvgl/fsmc_st7789.c` 文件的第 217 行，`fsmc_st7789_draw_pixel` 函数中调用了不存在的 `lcd_set_window` 函数：

```c
/* 绘制一个像素 */
void fsmc_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_set_window(x, y, x, y);  // 错误：调用了不存在的函数
    lcd_write_data_16bit(color);
}
```

而该文件中实际定义的函数名是 `fsmc_st7789_set_window`（第 43 行）：

```c
/* 设置显示区域 */
void fsmc_st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* 设置列地址 */
    lcd_write_cmd(0x2A);  
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);
    lcd_write_data(x2 >> 8);
    lcd_write_data(x2 & 0xFF);
    
    /* 设置行地址 */
    lcd_write_cmd(0x2B);  
    lcd_write_data(y1 >> 8);
    // ...
}
```

这个问题会导致：

* 如果编译时启用了链接器检查，会导致链接失败

* 即使当前未被使用，也会降低代码质量，在代码审查时造成困扰

* 如果未来有人调用 `fsmc_st7789_draw_pixel` 函数，会导致编译错误

# 2. 收益

修复这个函数调用错误后，可以消除潜在的编译失败风险，提高代码质量和可维护性。

## 2.1. **消除编译错误风险**

修复后，`fsmc_st7789_draw_pixel` 函数将能够正常编译和链接，避免未来调用该函数时出现编译错误。

## 2.2. **提高代码质量**

消除这个明显的错误调用，使代码更加规范和专业，减少代码审查时的困惑。

# 3. 方案

将 `fsmc_st7789_draw_pixel` 函数中的 `lcd_set_window` 调用更正为 `fsmc_st7789_set_window`。

## 3.1. **修正函数调用名称**

### 修改前

```c
/* 绘制一个像素 */
void fsmc_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_set_window(x, y, x, y);  // 错误：函数不存在
    lcd_write_data_16bit(color);
}
```

### 修改后

```c
/* 绘制一个像素 */
void fsmc_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    fsmc_st7789_set_window(x, y, x, y);  // 正确：调用实际存在的函数
    lcd_write_data_16bit(color);
}
```

修改说明：

* 将第 217 行的 `lcd_set_window(x, y, x, y)` 修改为 `fsmc_st7789_set_window(x, y, x, y)`

* 参数保持不变，因为两个函数的参数签名相同

* 这样修改后，函数调用将与实际定义的函数名一致

# 4. 回归范围

这个修改只涉及单个函数的调用更正，影响范围非常有限。

## 4.1. 主链路

* 如果项目中存在调用 `fsmc_st7789_draw_pixel` 函数的代码，需要验证该函数能够正常工作

* 测试基本的像素绘制功能，确认颜色显示正确

## 4.2. 边界情况

* 测试在屏幕边界位置（如 x=0, y=0 或 x=最大值, y=最大值）绘制像素

* 确认修改不会影响其他使用 `fsmc_st7789_set_window` 函数的地方

