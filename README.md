# gfx-mini-suite-2

图形与多媒体领域第二批小项目集合，每个子目录独立可构建，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-sat-poly | C11 | 2D 多边形 SAT 相交与点内判定 |
| projects/mini-gradient-map | C11 | Sobel 梯度幅值与边缘计数 |
| projects/mini-chroma-planar | C11 | RGB 与 YUV420 平面互转 |
| projects/mini-gain-stage | Python 3 | 静态增益与峰值均方根 |
| projects/mini-sprite-xform | C++17 | 精灵顶点变换、包围盒与按视口剔除 |

进入任一子目录执行 `make test` 即可。

## mini-sprite-xform 视口剔除

`src/cull.{h,cpp}` 在不改动原有 `xform_sprite` / `xform_bounds` 的前提下新增按视口剔除：

- `cull_box_intersects_viewport`：AABB 与视口的闭区间相交判定，贴边/贴角一律保留，完全在外才剔除；零尺寸精灵（点、水平线、垂直线）按同一规则处理。
- `cull_sprites`：批量剔除，返回保留数量，并通过两个输出数组分别给出保留项与剔除项的**原始索引**（均为升序，严格划分 `[0,n)`）；输出指针可为 `null`，不分配内存，O(n) 时间、O(1) 辅助空间。
- 确定性边界：空视口（零宽/零高、倒置、含 NaN）一律全剔除；`n == 0` 返回 0；倒置盒或含 NaN 的盒子保守保留，杜绝误删。
- 测试 `tests/test_cull.cpp` 含 2 万次随机对拍（与结构独立的参考实现交叉验证）、四组 CSV 场景（`tests/gen_data.cpp` 生成，内嵌 FNV-1a 校验和，`make data` 可复现）及变换→包围盒→剔除的端到端用例。
- `make bench` 给出四组百万精灵场景耗时（实测最坏单跑 < 9 ms，约 7.5 ns/精灵）。
