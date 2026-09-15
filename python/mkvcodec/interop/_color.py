"""Shared YUV conversion policy for optional GPU processor adapters."""

from __future__ import annotations


def resolve_yuv_conversion(
    color_space: str,
    color_range: str,
    height: int,
) -> tuple[str, str, tuple[float, float, float, float, float, float]]:
    """Resolve automatic metadata and return packed RGB coefficients.

    Returns
    -------
    tuple
        Effective color space, effective range, then Y offset/multiplier and
        red-V, green-U, green-V, blue-U coefficients.
    """
    selected_space = (
        ("bt709" if height >= 720 else "bt601")
        if color_space == "auto"
        else color_space
    )
    selected_range = "limited" if color_range == "auto" else color_range
    kr, kb = {
        "bt601": (0.2990, 0.1140),
        "bt709": (0.2126, 0.0722),
        "bt2020": (0.2627, 0.0593),
    }[selected_space]
    kg = 1.0 - kr - kb
    if selected_range == "limited":
        y_offset, y_multiplier, chroma_scale = 16.0, 255.0 / 219.0, 255.0 / 224.0
    else:
        y_offset, y_multiplier, chroma_scale = 0.0, 1.0, 1.0
    coefficients = (
        y_offset,
        y_multiplier,
        chroma_scale * 2.0 * (1.0 - kr),
        -chroma_scale * 2.0 * kb * (1.0 - kb) / kg,
        -chroma_scale * 2.0 * kr * (1.0 - kr) / kg,
        chroma_scale * 2.0 * (1.0 - kb),
    )
    return selected_space, selected_range, coefficients


__all__ = ["resolve_yuv_conversion"]
