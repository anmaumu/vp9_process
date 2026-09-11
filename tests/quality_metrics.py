"""Small deterministic image-quality metrics used by codec acceptance tests."""

from __future__ import annotations

import numpy as np


def luma_ssim(reference: np.ndarray, actual: np.ndarray, block_size: int = 8) -> float:
    """Calculate mean uniform-block SSIM for two 8-bit luma planes.

    Parameters
    ----------
    reference, actual:
        Two equally shaped two-dimensional luma arrays.
    block_size:
        Positive block edge. Partial edge blocks are included.

    Returns
    -------
    float
        Mean SSIM in the conventional ``[-1, 1]`` range.
    """
    if reference.ndim != 2 or actual.shape != reference.shape:
        raise ValueError("SSIM inputs must be equally shaped two-dimensional planes")
    if block_size <= 0:
        raise ValueError("SSIM block size must be positive")
    first = reference.astype(np.float64, copy=False)
    second = actual.astype(np.float64, copy=False)
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    scores: list[float] = []
    height, width = first.shape
    for top in range(0, height, block_size):
        for left in range(0, width, block_size):
            x = first[top:top + block_size, left:left + block_size]
            y = second[top:top + block_size, left:left + block_size]
            mean_x = float(x.mean())
            mean_y = float(y.mean())
            centered_x = x - mean_x
            centered_y = y - mean_y
            variance_x = float(np.mean(centered_x * centered_x))
            variance_y = float(np.mean(centered_y * centered_y))
            covariance = float(np.mean(centered_x * centered_y))
            numerator = (2 * mean_x * mean_y + c1) * (2 * covariance + c2)
            denominator = (mean_x * mean_x + mean_y * mean_y + c1) * (
                variance_x + variance_y + c2
            )
            scores.append(numerator / denominator)
    return float(np.clip(np.mean(scores), -1.0, 1.0))
