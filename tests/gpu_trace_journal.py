"""Opt-in qualification journal; never part of the shipped library.

CLOCK_MONOTONIC timestamps align only with perf record --clockid mono.
VA exports can perturb driver behavior, so their intervals are explicit.
"""
import json
import os
from pathlib import Path
import threading
import time


class TraceJournal:
    def __init__(self):
        path = os.environ.get("MKVC_GPU_TRACE_JOURNAL")
        self.path = Path(path) if path else None
        self.sequence = 0
        if self.path:
            self.path.write_text("", encoding="utf-8")

    def mark(self, phase, **metadata):
        if not self.path:
            return
        if self.sequence >= 10000:
            raise RuntimeError("Trace journal limit exceeded; use a short diagnostic run")
        entry = dict(version=1, clock="CLOCK_MONOTONIC", pid=os.getpid(),
                     tid=threading.get_native_id(), monotonic_ns=time.monotonic_ns(),
                     sequence=self.sequence, phase=phase, **metadata)
        self.sequence += 1
        with self.path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(entry) + "\n")

    def surface(self, role, display, surface_id, index=None):
        if not self.path:
            return
        from intel_va_prime_support import export_layout
        self.mark("diagnostic_export", role=role, frame=index)
        layout = export_layout(display, surface_id)
        self.mark("diagnostic_export_done", role=role, frame=index,
                  surface_id=surface_id, layout=layout)


journal = TraceJournal()
