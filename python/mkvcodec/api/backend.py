"""Backend discovery and deterministic selection API."""

from .._capabilities import backend_capabilities, select_backend
from .._types import BackendCapability

__all__ = ["BackendCapability", "backend_capabilities", "select_backend"]
