"""Native library loading, opaque handles, constants, and error translation."""

# The flat module remains the generated binding target during the compatibility
# window.  Keeping this facade as the dependency boundary lets the generator be
# relocated independently without changing high-level modules again.
from .._native import *  # noqa: F403
from .._native import check, lib

__all__ = ["check", "lib"]
