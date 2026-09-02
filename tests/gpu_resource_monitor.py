"""Test-only Linux DRM fdinfo observations; shared BO totals can overlap.

Reference: https://docs.kernel.org/gpu/drm-usage-stats.html
No root permissions or global profiling configuration are required.
"""
from pathlib import Path
import re


def parse_fdinfo(text):
    """Keep device/client identity and supported memory fields; never invent zero."""
    fields = dict(line.split(":", 1) for line in text.splitlines() if ":" in line)
    fields = {key: value.strip() for key, value in fields.items()}
    if not all(key in fields for key in ("drm-driver", "drm-client-id", "drm-pdev")):
        return None
    memory = {}
    for key, value in fields.items():
        if not key.startswith(("drm-total-", "drm-resident-", "drm-shared-")) or "cycles" in key:
            continue
        match = re.fullmatch(r"(\d+)(?:\s+(B|KiB|MiB|GiB))?", value)
        if not match:
            raise ValueError(f"Unknown DRM memory unit: {key}={value}")
        memory[key] = int(match[1]) * {None: 1, "B": 1, "KiB": 1024,
                                     "MiB": 1024**2, "GiB": 1024**3}[match[2]]
    return {"device": fields["drm-pdev"], "driver": fields["drm-driver"],
            "client": fields["drm-client-id"], "memory": memory}


def snapshot(root=Path("/proc/self/fdinfo")):
    """Deduplicate dup() descriptors by driver/device/client identity."""
    clients = {}
    for path in root.iterdir():
        try:
            client = parse_fdinfo(path.read_text())
        except FileNotFoundError:  # The descriptor used to enumerate may have closed.
            continue
        if client:
            clients[(client["driver"], client["device"], client["client"])] = client
    devices = {}
    for client in clients.values():
        device = devices.setdefault(client["device"], {"driver": client["driver"], "clients": 0, "memory": {}})
        device["clients"] += 1
        for key, value in client["memory"].items():
            device["memory"][key] = device["memory"].get(key, 0) + value
    return devices


class ResourceMonitor:
    """Bounded active/post-close samples with a conservative per-field growth gate."""
    def __init__(self, required=False):
        self.required = required
        self.processing_device = None
        self.report = {"source": "DRM fdinfo per-process client sums", "samples": 0,
                       "shared_allocations_may_be_double_counted": True,
                       "growth_budget_bytes": 256 * 1024**2, "phases": {}}

    def set_processing_device(self, identity):
        import os
        expected = os.environ.get("MKVC_TEST_GPU_PCI")
        if expected and identity.get("pci") != expected:
            raise RuntimeError(f"Wrong processing GPU: {identity}; expected {expected}")
        if self.processing_device and identity != self.processing_device:
            raise RuntimeError("Processing GPU changed during soak")
        self.processing_device = identity
        self.report["processing_device"] = identity

    def sample(self, phase):
        devices = snapshot()
        state = self.report["phases"].setdefault(phase, {})
        self.report["samples"] += 1
        target = devices.get((self.processing_device or {}).get("pci"), {})
        if self.required and phase == "active" and not any(
                key.startswith("drm-resident-vram") for key in target.get("memory", {})):
            raise RuntimeError("Required active VRAM fdinfo evidence is unavailable")
        for name, device in devices.items():
            entry = state.setdefault(name, {"driver": device["driver"],
                                            "baseline": device["memory"].copy(),
                                            "high_water": {}, "last": {}})
            entry["last"] = device["memory"]
            for key, value in device["memory"].items():
                if key not in entry["baseline"]:
                    raise RuntimeError(f"DRM observation schema changed: {name}/{key}")
                entry["high_water"][key] = max(entry["high_water"].get(key, 0), value)
                if value > entry["baseline"][key] + self.report["growth_budget_bytes"]:
                    raise RuntimeError(f"DRM memory growth exceeded budget: {name}/{key}")
        self.report["last_devices"] = devices
        return devices
