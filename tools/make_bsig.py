#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Генератор демонстрационной записи сеанса в формате .bsig (версия 3).

Раскладка файла описана в include/device/sessionrecord.h. Два условия,
нарушение которых ломает воспроизведение молча:

  * каналы в заголовке идут строго по возрастанию physIndex — PhantomDevice
    отклоняет команду SetChannels с неупорядоченным набором кодом 0x02;
  * sampleCount в заголовке совпадает с числом записанных наборов.

Сигнал синтетический, но каждая составляющая проверяет свою часть режима
просмотра: дрейф изолинии — фильтр верхних частот, наводка 50 и 150 Гц —
спектр и режекторный фильтр, пачки мышечной активности — осциллограмму.

Запуск:
    python3 tools/make_bsig.py -o recordings/demo-gestures.bsig
"""

import argparse
import math
import os
import random
import struct
import sys

MAGIC = b"BSIG"
VERSION = 3
# Усиление каналов — справочное сведение записи; ×6 — усиление устройства
# по умолчанию.
DEFAULT_GAIN = 6
VREF_MV = 2400

# Метки каналов: правая и левая группы мышц предплечья.
LABELS = [
    "FCR-R", "FCU-R", "ECR-R", "EDC-R",
    "FCR-L", "FCU-L", "ECR-L", "EDC-L",
]

# Жесты и вес каждого канала в них. Вес 1.0 — мышца работает в полную силу,
# 0.15 — почти покой. Разные наборы весов делают жесты различимыми глазом
# и пригодными как игрушечный вход для сбора набора данных.
GESTURES = [
    ("сжатие кисти",      [1.00, 0.85, 0.25, 0.30, 0.20, 0.15, 0.15, 0.20]),
    ("разжатие кисти",    [0.20, 0.25, 0.95, 1.00, 0.15, 0.20, 0.20, 0.15]),
    ("сгибание запястья", [0.90, 0.35, 0.20, 0.25, 0.85, 0.30, 0.20, 0.20]),
    ("поворот кисти",     [0.35, 0.90, 0.85, 0.30, 0.25, 0.20, 0.30, 0.25]),
]


class BandNoise:
    """Полосовой шум 20–250 Гц: белый шум через два звена второго порядка.

    Моделирует спектр поверхностной электромиограммы, основная энергия
    которой лежит именно в этой полосе.
    """

    def __init__(self, sample_rate, low_hz, high_hz, rng):
        self.rng = rng
        self.hp = _Biquad.high_pass(sample_rate, low_hz, 0.707)
        self.lp = _Biquad.low_pass(sample_rate, high_hz, 0.707)

    def next(self):
        return self.lp.process(self.hp.process(self.rng.gauss(0.0, 1.0)))


class _Biquad:
    """Биквадратное звено с коэффициентами по RBJ — как dsp::Biquad в изделии."""

    def __init__(self, b0, b1, b2, a1, a2):
        self.b0, self.b1, self.b2 = b0, b1, b2
        self.a1, self.a2 = a1, a2
        self.z1 = self.z2 = 0.0

    def process(self, x):
        y = self.b0 * x + self.z1
        self.z1 = self.b1 * x - self.a1 * y + self.z2
        self.z2 = self.b2 * x - self.a2 * y
        return y

    @staticmethod
    def _common(fs, f0, q):
        w0 = 2.0 * math.pi * f0 / fs
        alpha = math.sin(w0) / (2.0 * q)
        return w0, alpha, math.cos(w0)

    @classmethod
    def low_pass(cls, fs, f0, q):
        w0, alpha, cw = cls._common(fs, f0, q)
        a0 = 1 + alpha
        return cls((1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0,
                   -2 * cw / a0, (1 - alpha) / a0)

    @classmethod
    def high_pass(cls, fs, f0, q):
        w0, alpha, cw = cls._common(fs, f0, q)
        a0 = 1 + alpha
        return cls((1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0,
                   -2 * cw / a0, (1 - alpha) / a0)


def envelope(t_in_burst, burst_len):
    """Плавная огибающая пачки: нарастание и спад по косинусу.

    Резкий фронт дал бы широкополосный щелчок, которого в мышечном сигнале
    нет, и исказил бы спектр.
    """
    ramp = min(0.25 * burst_len, 0.20)
    if t_in_burst < ramp:
        return 0.5 * (1 - math.cos(math.pi * t_in_burst / ramp))
    if t_in_burst > burst_len - ramp:
        rest = burst_len - t_in_burst
        return 0.5 * (1 - math.cos(math.pi * rest / ramp))
    return 1.0


def generate(channels, sample_rate, seconds, seed,
             rest_s=1.5, burst_s=1.5, peak_uv=520.0, mains_uv=70.0):
    rng = random.Random(seed)
    total = int(round(sample_rate * seconds))
    cycle = rest_s + burst_s

    noise = [BandNoise(sample_rate, 20.0, 250.0, rng) for _ in range(channels)]
    drift_phase = [rng.uniform(0, 2 * math.pi) for _ in range(channels)]
    mains_phase = rng.uniform(0, 2 * math.pi)
    offset = [rng.uniform(-40.0, 40.0) for _ in range(channels)]

    data = []
    for n in range(total):
        t = n / sample_rate
        pos = t % cycle
        active = pos >= rest_s
        gesture = GESTURES[int(t // cycle) % len(GESTURES)]
        gain = envelope(pos - rest_s, burst_s) if active else 0.0

        row = []
        for c in range(channels):
            # дрейф изолинии — убирается фильтром верхних частот
            v = 80.0 * math.sin(2 * math.pi * 0.25 * t + drift_phase[c])
            v += offset[c]
            # Сетевая наводка и её третья гармоника. Уровень взят как при
            # плохом контакте электрода: приложение считает спектр по всему
            # циклу развёртки, где половину времени занимают пачки, и более
            # слабая наводка утонула бы в их спектре.
            v += mains_uv * math.sin(2 * math.pi * 50.0 * t + mains_phase)
            v += mains_uv / 2.8 * math.sin(2 * math.pi * 150.0 * t + mains_phase)
            # фоновый шум тракта
            v += rng.gauss(0.0, 8.0)
            # мышечная активность
            weight = gesture[1][c] if c < len(gesture[1]) else 0.2
            v += noise[c].next() * peak_uv * weight * gain
            row.append(int(round(v)))
        data.append(row)
    return data


def write_bsig(path, data, channels, sample_rate, description, gain=DEFAULT_GAIN):
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HHIHH", VERSION, channels, sample_rate, VREF_MV, 0))
        desc = description.encode("utf-8")
        f.write(struct.pack("<I", len(desc)))
        f.write(desc)
        for c in range(channels):
            label = LABELS[c % len(LABELS)].encode("utf-8")
            # physIndex строго по возрастанию: иначе SetChannels отклонится
            f.write(struct.pack("<BBBH", c, 1, gain, len(label)))
            f.write(label)
        f.write(struct.pack("<Q", len(data)))
        for row in data:
            f.write(struct.pack("<%di" % channels, *row))


def verify(path):
    """Перечитывает файл по документированной раскладке и печатает сводку."""
    with open(path, "rb") as f:
        blob = f.read()
    assert blob[:4] == MAGIC, "нет сигнатуры BSIG"
    pos = 4
    version, channels, rate, vref, _res = struct.unpack_from("<HHIHH", blob, pos)
    pos += 12
    assert version == VERSION, "версия %d, ожидалась %d" % (version, VERSION)
    assert 1 <= channels <= 8, "недопустимое число каналов: %d" % channels
    (desc_len,) = struct.unpack_from("<I", blob, pos)
    pos += 4
    description = blob[pos:pos + desc_len].decode("utf-8")
    pos += desc_len

    phys, labels, gains = [], [], []
    for _ in range(channels):
        p, enabled, gain, label_len = struct.unpack_from("<BBBH", blob, pos)
        pos += 5
        gains.append(gain)
        labels.append(blob[pos:pos + label_len].decode("utf-8"))
        pos += label_len
        phys.append(p)
    assert all(b > a for a, b in zip(phys, phys[1:])), \
        "каналы не по возрастанию: %s — SetChannels будет отклонён" % phys

    (count,) = struct.unpack_from("<Q", blob, pos)
    pos += 8
    expected = count * channels * 4
    assert len(blob) - pos == expected, \
        "данных %d Б, по sampleCount ожидалось %d Б" % (len(blob) - pos, expected)

    lo = [10 ** 9] * channels
    hi = [-10 ** 9] * channels
    for n in range(count):
        row = struct.unpack_from("<%di" % channels, blob, pos + n * channels * 4)
        for c, v in enumerate(row):
            lo[c] = min(lo[c], v)
            hi[c] = max(hi[c], v)

    print("файл:        %s (%.1f МБ)" % (path, len(blob) / 1048576.0))
    print("версия:      %d, каналов: %d, частота: %d Гц, опора: %d мВ"
          % (version, channels, rate, vref))
    print("наборов:     %d (%.1f с)" % (count, count / float(rate)))
    print("описание:    %s" % description.replace("\n", " "))
    print("каналы:      %s" % ", ".join("%d:%s ×%d" % (p, l, g) for p, l, g in zip(phys, labels, gains)))
    print("амплитуды, мкВ:")
    for c in range(channels):
        print("  %-6s %+7d … %+7d" % (labels[c], lo[c], hi[c]))
    assert max(abs(min(lo)), abs(max(hi))) < 8388607, "выход за 24 бита"
    print("проверка пройдена")


def main():
    ap = argparse.ArgumentParser(description="Генератор записи сеанса .bsig")
    ap.add_argument("-o", "--output", default="recordings/demo-gestures.bsig")
    ap.add_argument("-c", "--channels", type=int, default=8)
    ap.add_argument("-r", "--rate", type=int, default=1000)
    ap.add_argument("-d", "--seconds", type=float, default=30.0)
    ap.add_argument("-s", "--seed", type=int, default=20260923)
    ap.add_argument("-g", "--gain", type=int, default=DEFAULT_GAIN,
                    help="усиление каналов, записываемое в заголовок")
    ap.add_argument("-m", "--mains", type=float, default=70.0,
                    help="амплитуда сетевой наводки 50 Гц, мкВ")
    args = ap.parse_args()

    if not 1 <= args.channels <= 8:
        sys.exit("число каналов должно быть от 1 до 8")

    names = ", ".join(g[0] for g in GESTURES)
    description = (
        "Демонстрационная синтетическая запись. Пачки мышечной активности "
        "(%s) чередуются с покоем; наложены дрейф изолинии 0,25 Гц, сетевая "
        "наводка 50 и 150 Гц и фоновый шум. Предназначена для проверки "
        "просмотра сигналов без аппаратуры." % names)

    data = generate(args.channels, args.rate, args.seconds, args.seed,
                    mains_uv=args.mains)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    write_bsig(args.output, data, args.channels, args.rate, description, args.gain)
    verify(args.output)


if __name__ == "__main__":
    main()
