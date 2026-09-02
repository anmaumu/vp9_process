"""Noninteractive FFmpeg/ffprobe calls for file-based hardware test oracles."""
from pathlib import Path
import subprocess


def run_oracle(command):
    """Do not inherit a terminal: background reads can SIGTTIN the whole test.

These oracles read media files, never stdin. Preserve stdout/stderr capture and
the existing 60-second check/timeout contract.
"""
    command = list(command)
    executable = Path(command[0]).name.lower()
    if executable in ("ffmpeg", "ffmpeg.exe"):
        command.insert(1, "-nostdin")
    elif executable not in ("ffprobe", "ffprobe.exe"):
        raise ValueError("Only FFmpeg and ffprobe file-based oracles are supported")
    return subprocess.run(command, stdin=subprocess.DEVNULL, check=True,
                          capture_output=True, timeout=60)
