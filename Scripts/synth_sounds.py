# Writes the Ctrl-click sounds to Scripts/Sounds as 16-bit mono WAVs. Standard library only; re-runnable.
# Import them afterwards with Scripts/import_sounds.py.
#   python Scripts/synth_sounds.py
import math
import os
import random
import struct
import wave

RATE = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Sounds")


def tone(freq, length, decay, harmonics=((1, 1.0),), attack=0.003, glide=0.0):
    """A plucked tone: quick attack, exponential decay. glide bends the pitch by that fraction over the note."""
    samples = []
    phase = [0.0] * len(harmonics)
    for i in range(int(length * RATE)):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-t / decay)
        f = freq * (1.0 + glide * t / length)
        value = 0.0
        for k, (mult, gain) in enumerate(harmonics):
            phase[k] += 2.0 * math.pi * f * mult / RATE
            value += gain * math.sin(phase[k])
        samples.append(value * env)
    return samples


def silence(length):
    return [0.0] * int(length * RATE)


def mix(a, b, gain_b=1.0):
    out = list(a) + [0.0] * max(0, len(b) - len(a))
    for i, v in enumerate(b):
        out[i] += v * gain_b
    return out


def click(length=0.004, seed=7):
    rng = random.Random(seed)
    n = int(length * RATE)
    return [rng.uniform(-1.0, 1.0) * (1.0 - i / n) for i in range(n)]


def write(name, samples, peak):
    # A short fade at the end so nothing pops when the sound stops.
    fade = int(0.004 * RATE)
    for i in range(fade):
        samples[-1 - i] *= i / fade
    top = max(abs(v) for v in samples) or 1.0
    data = b"".join(struct.pack("<h", int(v / top * peak * 32767)) for v in samples)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(data)
    print(f"{path}  {len(samples) / RATE * 1000:.0f} ms")


BELL = ((1, 1.0), (2, 0.25), (3, 0.08))
WARM = ((1, 1.0), (3, 0.3), (5, 0.12))

# Copy: two quick rising blips, "picked up".
copy = tone(784, 0.06, 0.018, BELL) + silence(0.012) + tone(1175, 0.09, 0.025, BELL)
write("S_Atlas_Copy", copy, 0.5)

# Paste: one stamped note with a soft tick on the front, "put down".
paste = mix(tone(659, 0.13, 0.035, BELL, glide=-0.06), click(), 0.25)
write("S_Atlas_Paste", paste, 0.5)

# Error: two low falling notes.
error = tone(233, 0.09, 0.04, WARM) + silence(0.02) + tone(185, 0.14, 0.05, WARM)
write("S_Atlas_Error", error, 0.42)
