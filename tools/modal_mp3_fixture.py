"""Generate a public-domain synthetic MP3 test tone on Modal."""
from pathlib import Path
import subprocess
import modal

app = modal.App("beat-scholar-synthetic-mp3")
image = modal.Image.debian_slim().apt_install("ffmpeg")

@app.function(image=image, cpu=1, memory=512, timeout=120)
def generate() -> bytes:
    target = Path("/tmp/synthetic-tone.mp3")
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "sine=frequency=440:sample_rate=44100:duration=0.2",
                    "-ac", "2", "-codec:a", "libmp3lame", "-b:a", "128k", str(target)], check=True)
    return target.read_bytes()

@app.local_entrypoint()
def main():
    target = Path(__file__).resolve().parents[1] / "tests/fixtures/synthetic-tone.mp3"
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as output:
        output.write(generate.remote())
    print(f"Synthetic fixture saved: {target.name} ({target.stat().st_size} bytes)")
