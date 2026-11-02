"""Record what the default Windows output device plays (WASAPI loopback) to a stereo 16-bit WAV.

Usage: record_loopback.py OUT.wav START_FILE

START_FILE receives the epoch time (seconds) at which capturing began, so the audio can be
aligned with a separately recorded video. Recording stops when a line (or EOF) arrives on stdin.
"""
import array
import os
import sys
import threading
import time
import wave

import pyaudiowpatch as pyaudio


def main() -> int:
    out_path, start_path = sys.argv[1], sys.argv[2]

    stop = threading.Event()

    def watch_stdin() -> None:
        sys.stdin.readline()
        stop.set()

    threading.Thread(target=watch_stdin, daemon=True).start()

    pa = pyaudio.PyAudio()
    device = pa.get_default_wasapi_loopback()
    rate = int(device["defaultSampleRate"])
    channels = int(device["maxInputChannels"])
    print(f"Capturing audio from: {device['name']} ({rate} Hz, {channels} ch)", flush=True)

    wav = wave.open(out_path, "wb")
    wav.setnchannels(2)
    wav.setsampwidth(2)
    wav.setframerate(rate)

    frames_written = 0
    t0_mono = 0.0

    def pad_to(total_frames: int) -> None:
        nonlocal frames_written
        missing = total_frames - frames_written
        if missing > 0:
            wav.writeframes(bytes(missing * 4))
            frames_written += missing

    def to_stereo(data: bytes, n: int) -> array.array:
        samples = array.array("h")
        samples.frombytes(data)
        if channels == 2:
            return samples
        out = array.array("h", bytes(n * 4))
        out[0::2] = samples[0::channels]
        out[1::2] = samples[1 % channels::channels]
        return out

    def callback(in_data, frame_count, time_info, status):
        nonlocal frames_written
        n = len(in_data) // (2 * channels)
        # Loopback delivers nothing while the device is silent; fill the gap to keep sync.
        expected = int((time.monotonic() - t0_mono) * rate)
        if expected - n - frames_written > rate // 10:
            pad_to(expected - n)
        wav.writeframes(to_stereo(in_data, n).tobytes())
        frames_written += n
        return (None, pyaudio.paContinue)

    stream = pa.open(
        format=pyaudio.paInt16,
        channels=channels,
        rate=rate,
        frames_per_buffer=1024,
        input=True,
        input_device_index=device["index"],
        stream_callback=callback,
        start=False,
    )

    t0_mono = time.monotonic()
    t0_epoch = time.time()
    stream.start_stream()

    tmp = start_path + ".tmp"
    with open(tmp, "w") as f:
        f.write(repr(t0_epoch))
    os.replace(tmp, start_path)

    stop.wait()

    stream.stop_stream()
    stream.close()
    pad_to(int((time.monotonic() - t0_mono) * rate))
    wav.close()
    pa.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
