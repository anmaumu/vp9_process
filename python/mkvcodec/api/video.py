"""Container video metadata API."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class VideoInfo:
    """Describe the first supported video track without decoding it.

    Attributes
    ----------
    codec : str
        ``"vp9"`` or ``"av1"``.
    width, height : int
        Coded dimensions in pixels.
    fps : float or None
        Nominal frame rate when available.
    duration_ns, frame_count : int or None
        Container duration and selected-track frame count when available.
    """

    codec: str
    width: int
    height: int
    fps: float | None
    duration_ns: int | None
    frame_count: int | None


from ..internal.video_info import probe_video  # noqa: E402

__all__ = ["VideoInfo", "probe_video"]
