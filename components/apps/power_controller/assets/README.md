# PowerController Assets

此目录包含PowerController应用的资源文件。

## 文件说明

### 图标文件
- `power_icon.png` - 应用主图标 (建议尺寸: 64x64 像素)
- `battery_icon.png` - 电池图标 
- `charging_icon.png` - 充电状态图标
- `power_mode_icon.png` - 电源模式图标
- `brightness_icon.png` - 亮度控制图标

### UI资源
- `background.png` - 应用背景图
- `button_*.png` - 各种按钮素材
- `gauge_*.png` - 仪表盘素材

## 使用说明

1. 将图片文件放入此目录
2. 使用LVGL的图片转换工具生成.c文件
3. 在PowerController.cpp中声明和使用图标

## 图片格式要求

- 格式：PNG (推荐) 或 BMP
- 色深：建议16位或24位
- 尺寸：根据屏幕分辨率适配
- 透明度：PNG格式支持透明背景

## 转换命令示例

```bash
# 使用LVGL工具转换图片
python path/to/lvgl/scripts/img_conv.py power_icon.png -f true_color_alpha -o img_app_power_controller.c
```

## 注意事项

- 图片文件会影响Flash存储空间，请合理控制文件大小
- 建议使用压缩工具减小PNG文件体积
- 图标设计应与系统整体风格保持一致