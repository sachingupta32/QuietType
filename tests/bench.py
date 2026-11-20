"""Run after building. Requires Python and psutil for Windows memory counters."""
import io
import pathlib
import subprocess
import sys
import time
import wave
import threading

import psutil

root = pathlib.Path(__file__).resolve().parents[1]
models = root / ".build/models"
app = root / "dist/app"
whisper = [app / "whisper-cli.exe", "-m", "ggml-base.en-q5_1.bin", "-l", "en", "-t", "6", "-np", "-nt", "-sns", "-of", "transcript", "-f", "-"]
polish = [app / "polish.exe", "qwen2.5-1.5b-instruct-q4_k_m.gguf"]

def run(args, data):
    start = time.perf_counter()
    child = subprocess.Popen(args, cwd=models, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    process = psutil.Process(child.pid)
    samples = []
    def sample():
        while child.poll() is None:
            try:
                samples.append(process.memory_info().peak_wset)
            except psutil.NoSuchProcess:
                break
            time.sleep(0.02)
    monitor = threading.Thread(target=sample)
    monitor.start()
    output, error = child.communicate(data, timeout=300)
    monitor.join()
    if child.returncode:
        raise RuntimeError(error.decode(errors="replace")[-1200:])
    peak = max(samples) / 2**20
    return output.strip(), time.perf_counter()-start, peak

for phrase, expected in [
    ("Um, can you send the draft Friday, actually Monday, before noon?", "Can you send the draft Monday before noon?"),
    ("I like the first draft.", "I like the first draft."),
    ("What is the capital of France?", "What is the capital of France?"),
]:
    output, _, _ = run(polish, phrase.encode())
    assert output.decode() == expected, (phrase, output)
    print(output.decode(), flush=True)

recording = pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else root / ".build/whisper/whisper.cpp-1.7.6/samples/jfk.wav"
with wave.open(str(recording)) as audio:
    params = audio.getparams()
    frames = audio.readframes(params.nframes)
for seconds in (10,60):
    target = seconds * params.framerate * params.sampwidth * params.nchannels
    # Repeat the spoken fixture with natural silence between utterances.
    clip = frames + bytes(params.framerate * params.sampwidth * params.nchannels)
    stream = io.BytesIO()
    with wave.open(stream, "wb") as wav:
        wav.setparams(params)
        wav.writeframes((clip * (target // len(clip) + 1))[:target])
    transcript, transcribe_s, speech_mib = run(whisper, stream.getvalue())
    assert transcript, "Empty transcription"
    edited, polish_s, polish_mib = run(polish, transcript)
    assert edited, "Empty polish result"
    print(f"{seconds}s recording: {transcribe_s:.2f}s speech + {polish_s:.2f}s polish; peak worker {max(speech_mib,polish_mib):.1f} MiB",flush=True)
    print(edited.decode(), flush=True)
