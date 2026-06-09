#!/usr/bin/env python3
"""
preview_alert.py — 경고음을 PC 스피커로 들어보는 미리듣기/테스트 도구.

두 가지 모드:
  1) 기기 실제 출력 미리듣기 (기본)
     alert_pcm.h 를 그대로 복원해 '아두이노가 낼 소리'(22050Hz/12bit, 펌웨어 gap)를 재생.
       python3 preview_alert.py
       python3 preview_alert.py --gap 90 --times 8

  2) 합성 A/B 비교 (sound.py 에서 즉석 합성, .h 건드리지 않음)
     insurance(ANC 골짜기 780Hz 톤) gain 을 바꿔가며 귀로 비교 → 값 확정용.
       python3 preview_alert.py --ab 0.3 0.9
       python3 preview_alert.py --ab 0.3 0.6 0.9 1.2

확정되면 gen_alert_pcm.py 의 INSURANCE_GAIN/INSURANCE_HZ 를 맞추고 재생성:
  python3 gen_alert_pcm.py

재생: mac 내장 afplay 우선, 없으면 sounddevice 폴백.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ALERT_H = os.path.join(HERE, "alert_pcm.h")
SOUND_PY = os.environ.get("SOUND_PY", "/Users/jangjunhyeok/python/sound.py")
INSURANCE_HZ = 780        # gen_alert_pcm.py 와 동일 (옛 DuoBell 값)


def play_wav(path):
    """mac afplay 우선, 없으면 sounddevice."""
    afplay = subprocess.run(["which", "afplay"], capture_output=True, text=True)
    if afplay.returncode == 0:
        subprocess.run(["afplay", path])
        return
    try:
        import sounddevice as sd
        with wave.open(path) as w:
            rate = w.getframerate()
            data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        sd.play(data, rate)
        sd.wait()
    except Exception as e:  # noqa: BLE001
        print(f"재생 도구를 못 찾음({e}). WAV 만 저장됨: {path}")


def save_wav(path, pcm16, rate):
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm16.astype(np.int16).tobytes())


def pulse_train(pulse16, rate, times, gap_ms):
    gap = np.zeros(int(rate * gap_ms / 1000.0), dtype=np.int16)
    chunks = []
    for i in range(times):
        chunks.append(pulse16)
        if i < times - 1:
            chunks.append(gap)
    return np.concatenate(chunks)


def load_sound_module():
    sys.path.insert(0, os.path.dirname(SOUND_PY))
    import sound  # noqa: E402
    _orig = sound.insurance_low
    sound.insurance_low = (
        lambda duration=0.15, f0=INSURANCE_HZ, decay=5.0: _orig(duration, f0, decay)
    )
    return sound


def preview_device(times, gap_ms):
    """alert_pcm.h 를 복원해 기기 실제 출력을 재생."""
    txt = open(ALERT_H).read()
    rate = int(re.search(r"ALERT_RATE\s+(\d+)", txt).group(1))
    body = txt.split("{", 1)[1].split("};", 1)[0]
    nums = np.array(list(map(int, re.findall(r"\d+", body))), dtype=np.int32)
    pcm16 = ((nums - 2048) << 4).clip(-32768, 32767).astype(np.int16)  # 12bit→16bit
    train = pulse_train(pcm16, rate, times, gap_ms)
    out = os.path.join(tempfile.gettempdir(), "alert_device.wav")
    save_wav(out, train, rate)
    print(f"[기기 실제 출력] {rate}Hz/12bit, gap {gap_ms}ms, {times}발 → {out}")
    play_wav(out)


def preview_ab(gains, times, gap_ms):
    """sound.py 에서 insurance gain 별로 합성해 차례로 재생 (A/B)."""
    sound = load_sound_module()
    for g in gains:
        bell = sound.make_alert(duration=0.15, mode="normal", gains=dict(insurance=g))
        train = sound.repeat(bell, times=times, gap_sec=gap_ms / 1000.0)
        pcm16 = sound._to_int16(train)
        out = os.path.join(tempfile.gettempdir(), f"alert_ab_{g}.wav")
        save_wav(out, pcm16, sound.SAMPLE_RATE)
        print(f"[A/B] insurance={g} @ {INSURANCE_HZ}Hz → {out}")
        play_wav(out)


def main():
    ap = argparse.ArgumentParser(description="경고음 미리듣기/테스트")
    ap.add_argument("--ab", nargs="+", type=float, metavar="GAIN",
                    help="sound.py 즉석 합성으로 insurance gain A/B 비교")
    ap.add_argument("--times", type=int, default=8, help="펄스(발) 수")
    ap.add_argument("--gap", type=float, default=90.0, help="펄스 사이 무음 ms (펌웨어=90)")
    args = ap.parse_args()

    if args.ab:
        preview_ab(args.ab, args.times, args.gap)
    else:
        preview_device(args.times, args.gap)


if __name__ == "__main__":
    main()
