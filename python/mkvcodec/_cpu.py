"""Compatibility imports for :mod:`mkvcodec.internal.cpu_pool`."""

from .internal.cpu_pool import BorrowedCpuFrame, CpuBuffer, CpuFramePool, Submission

__all__ = ["BorrowedCpuFrame", "CpuBuffer", "CpuFramePool", "Submission"]
