"""DLPack extension discovery shared by GPU frame adapters."""

try:
    from .. import _dlpack as extension
except ImportError:
    extension = None

__all__: list[str] = []
