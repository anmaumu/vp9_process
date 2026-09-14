"""Compatibility imports for the split capture and writer modules."""

from .api.capture import VideoCapture
from .api.writer import VideoWriter

__all__ = ["VideoCapture", "VideoWriter"]
