# gfx-mini-suite-2

图形与多媒体领域第二批小项目集合，每个子目录独立可构建，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-sat-poly | C11 | 2D 多边形 SAT 相交与点内判定 |
| projects/mini-gradient-map | C11 | Sobel 梯度幅值与边缘计数 |
| projects/mini-chroma-planar | C11 | RGB 与 YUV420 平面互转 |
| projects/mini-gain-stage | Python 3 | 静态增益与峰值均方根；优先级仲裁侧链压缩（ducking） |
| projects/mini-sprite-xform | C++17 | 精灵顶点变换与包围盒 |

进入任一子目录执行 `make test` 即可。
