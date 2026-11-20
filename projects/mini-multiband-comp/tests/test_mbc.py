import math
import sys
sys.path.insert(0, "src")
import mbc


SR = 48000.0


def _expect_value_error(fn, label):
    try:
        fn()
    except ValueError:
        return
    raise AssertionError("expected ValueError for " + label)


def test_bypass_identical_output():
    # 关闭分带：不经过任何处理，输出与输入逐浮点相同
    s = [0.9 * math.sin(2 * math.pi * 120 * i / SR)
         + 0.3 * math.sin(2 * math.pi * 5000 * i / SR)
         for i in range(2000)]
    stereo = [v for x in s for v in (x, -0.8 * x)]
    out, stats = mbc.multiband_compress(stereo, 2, SR, enabled=False)
    assert out == stereo
    assert stats["enabled"] is False
    assert stats["low_gr_db"] == 0.0 and stats["high_gr_db"] == 0.0


def test_split_merge_reconstruction():
    # 互补分频：lp + hp 必须等于原信号（误差仅为浮点舍入）
    s = [0.9 * math.sin(2 * math.pi * 60 * i / SR)
         + 0.4 * math.sin(2 * math.pi * 4000 * i / SR)
         for i in range(4000)]
    bands = mbc.split_mono(s, [200.0], SR)
    recon = [sum(b[i] for b in bands) for i in range(len(s))]
    err = max(abs(a - b) for a, b in zip(s, recon))
    assert err < 1e-12, err
    assert len(bands) == 2


def test_static_curve_handcalc():
    # 手算：env=0.8, T=0.5, R=2, instant 包络
    # g = (0.8/0.5)^(1/2-1) = 1.6^-0.5 = sqrt(0.625) = 0.7905694150
    # y = 0.8 * g = 0.6324555320
    out, g = mbc.compress_band([0.8], 0.5, 2.0, attack=0.0, release=0.0,
                               sr=SR)
    assert abs(out[0] - 0.6324555320) < 1e-9, out[0]
    assert abs(g - 0.7905694150) < 1e-9, g
    assert abs(mbc.gain_reduction_db(g) - 2.04119983) < 1e-6
    # 阈值以下不压缩
    out0, g0 = mbc.compress_band([0.4], 0.5, 2.0, attack=0.0, release=0.0,
                                 sr=SR)
    assert out0 == [0.4] and g0 == 1.0
    assert mbc.gain_reduction_db(g0) == 0.0


def test_dc_pipeline_handcalc():
    # 手算整条链路（直流 0.8，cutoff 200Hz）：
    # 稳态下 lp -> 0.8、hp -> 0；低频按 T=0.5,R=2 压到 0.6324555，
    # 高频无声不压缩，合并后末帧即 0.6324555。
    n = 4000
    sig = [0.8] * n
    lp, hp = mbc.split_mono(sig, [200.0], SR)
    assert abs(lp[-1] - 0.8) < 1e-12
    assert abs(hp[-1]) < 1e-12
    out, stats = mbc.multiband_compress(sig, 1, SR, enabled=True,
                                        cutoff=200.0, low_thr=0.5, low_ratio=2.0,
                                        high_thr=0.5, high_ratio=2.0,
                                        attack=0.0, release=0.0)
    # 稳态（尾段）对照手算值；开头的高频带瞬态只影响前若干帧
    tail = out[-200:]
    assert all(abs(v - 0.6324555320) < 1e-9 for v in tail)
    assert abs(stats["low_min_gain"] - 0.7905694150) < 1e-9
    # 高频带只在开头有瞬态：hp0=(1-a)*0.8=0.7793...，被瞬时包络压一次，
    # 之后 hp -> 0、增益回到 1.0。手算该瞬态增益：
    hp0 = (1.0 - (1.0 - math.exp(-2.0 * math.pi * 200.0 / SR))) * 0.8
    hand_hg = (hp0 / 0.5) ** -0.5
    assert abs(stats["high_min_gain"] - hand_hg) < 1e-9
    assert abs(stats["high_min_gain"] - 0.8012) < 1e-3
    assert stats["overload"] is False


def test_crossover_first_sample_handcalc():
    # 独立标量递推复核分频器首帧：
    # a = 1 - exp(-2*pi*200/48000) = 0.0258402...
    # x0=0.9 -> lp0 = a*0.9 = 0.0232562..., hp0 = (1-a)*0.9 = 0.8767438...
    a = 1.0 - math.exp(-2.0 * math.pi * 200.0 / SR)
    lp, hp = mbc.split_mono([0.9], [200.0], SR)
    assert abs(lp[0] - a * 0.9) < 1e-15
    assert abs(hp[0] - (1.0 - a) * 0.9) < 1e-15
    assert abs(lp[0] - 0.0232562) < 1e-6
    assert abs(hp[0] - 0.8767438) < 1e-6
    # 第二帧手算：x1=-0.4
    y1 = a * 0.9
    y2 = y1 + a * (-0.4 - y1)
    lp2, _ = mbc.split_mono([0.9, -0.4], [200.0], SR)
    assert abs(lp2[1] - y2) < 1e-15


def test_release_envelope_bounded():
    # 回归：release 分支符号曾写反，衰减信号会让包络指数爆炸、增益下溢为 0
    sig = [0.9 * math.exp(-i / 500.0) for i in range(4000)]
    for tau in (0.0, 0.001, 0.05):
        out, g = mbc.compress_band(sig, 0.5, 2.0,
                                   attack=0.0, release=tau, sr=SR)
        assert g > 0.0, "zero gain with release=%r" % tau
        assert all(math.isfinite(v) for v in out)
        assert max(abs(v) for v in out) <= 0.9 * (1.0 + 1e-12)


def test_inverted_boundary_errors():
    _expect_value_error(lambda: mbc.validate_edges([800.0, 200.0], SR),
                        "inverted edges")
    _expect_value_error(lambda: mbc.validate_edges([200.0, 200.0], SR),
                        "duplicate edges")
    _expect_value_error(lambda: mbc.validate_edges([0.0], SR),
                        "edge at DC")
    _expect_value_error(lambda: mbc.validate_edges([SR / 2.0], SR),
                        "edge at nyquist")
    _expect_value_error(lambda: mbc.validate_edges([SR], SR),
                        "edge above nyquist")
    _expect_value_error(
        lambda: mbc.multiband_compress([0.0] * 8, 1, SR, cutoff=SR),
        "pipeline with bad cutoff")


def test_zero_ratio_errors():
    _expect_value_error(lambda: mbc.compress_band([0.8], 0.5, 0.0, sr=SR),
                        "zero ratio")
    _expect_value_error(lambda: mbc.compress_band([0.8], 0.5, -2.0, sr=SR),
                        "negative ratio")
    _expect_value_error(
        lambda: mbc.multiband_compress([0.5] * 8, 1, SR, low_ratio=0.0),
        "pipeline with zero low ratio")
    _expect_value_error(
        lambda: mbc.multiband_compress([0.5] * 8, 1, SR, high_ratio=0.0),
        "pipeline with zero high ratio")


def test_channel_count_mismatch():
    _expect_value_error(
        lambda: mbc.multiband_compress([0.1] * 5, 2, SR),
        "5 samples over 2 channels")
    _expect_value_error(
        lambda: mbc.multiband_compress([0.1] * 4, 6, SR),
        "4 samples over 6 channels")
    _expect_value_error(
        lambda: mbc.multiband_compress([0.1], 0, SR),
        "zero channels")


def test_overload_reported():
    # makeup 把已压缩的低频推过满刻度：峰值过载必须被报出
    sig = [0.8] * 2000
    out, stats = mbc.multiband_compress(
        sig, 1, SR, low_thr=0.5, low_ratio=2.0, makeup=2.0,
        attack=0.0, release=0.0)
    assert stats["overload"] is True
    assert stats["peak"] > 1.0
    # 稳态峰值 = 2 * 手算低频输出；全局峰值还包含开头高频带瞬态
    tail_peak = max(abs(v) for v in out[-200:])
    assert abs(tail_peak - 2.0 * 0.6324555320) < 1e-9
    # 无 makeup 时不过载
    _, stats2 = mbc.multiband_compress(
        sig, 1, SR, low_thr=0.5, low_ratio=2.0, attack=0.0, release=0.0)
    assert stats2["overload"] is False


def test_2ch_and_6ch_layout():
    n = 1024
    mono = [0.9 * math.sin(2 * math.pi * 55 * i / SR)
            + 0.4 * math.sin(2 * math.pi * 3000 * i / SR)
            for i in range(n)]
    for ch in (2, 6):
        samples = [0.0] * (n * ch)
        for c in range(ch):
            samples[c::ch] = [v * (1.0 - 0.1 * c) for v in mono]
        out, stats = mbc.multiband_compress(samples, ch, SR)
        assert len(out) == len(samples)
        assert len(stats["per_channel_peak"]) == ch
        assert stats["low_gr_db"] > 0.0   # 55Hz 低频分量确实被压
        # makeup=1 时压缩只会衰减：输出峰值不得超过输入峰值
        in_peak = max(abs(v) for v in samples)
        assert stats["peak"] <= in_peak + 1e-12
        # 过载标志必须与实际峰值一致
        assert stats["overload"] == (stats["peak"] > 1.0)
        # 旁路峰值与输入峰值一致
        _, bypass = mbc.multiband_compress(samples, ch, SR, enabled=False)
        assert abs(bypass["peak"] - in_peak) < 1e-12


if __name__ == "__main__":
    failed = total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn(); print("PASS", name)
            except AssertionError as e:
                failed += 1; print("FAIL", name, e)
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
