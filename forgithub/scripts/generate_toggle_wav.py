"""Gera assets/toggle.wav — executar uma vez ou substituir o ficheiro manualmente."""
import math
import struct
import wave
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "toggle.wav"
OUT.parent.mkdir(parents=True, exist_ok=True)

SAMPLE_RATE = 22050
DURATION = 0.18
FREQ = 880.0
AMPLITUDE = 12000

samples = []
for i in range(int(SAMPLE_RATE * DURATION)):
    t = i / SAMPLE_RATE
    envelope = min(1.0, (int(SAMPLE_RATE * DURATION) - i) / (SAMPLE_RATE * 0.05))
    value = int(math.sin(2 * math.pi * FREQ * t) * AMPLITUDE * envelope)
    samples.append(struct.pack("<h", value))

with wave.open(str(OUT), "w") as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(b"".join(samples))

print(f"Criado: {OUT} ({OUT.stat().st_size} bytes)")
