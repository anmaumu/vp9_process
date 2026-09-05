# ABI governance

`include/mkvcodec/mkvc.h` is the stable cross-language contract. The reviewed
snapshot in `abi/mkvc-abi-v1.json` fingerprints every exported function
signature, enum value and public struct field in declaration order. Exported
symbol names and declaration counts remain directly reviewable in the snapshot.

Run:

```text
python tools/abi_guard.py check
```

Any difference fails closed. A maintainer must decide whether the change is
backward compatible, requires a new versioned struct/function, or requires an
ABI version increment. Only then may a new snapshot be reviewed and committed.
The snapshot does not replace compiled `sizeof`/`offsetof` tests on Windows and
Linux; both checks are required.

Python public APIs use NumPy-style docstrings. C and C++ public/internal APIs use
Doxygen comments. Formatting-only changes and structural refactors are committed
separately so behavioral review remains tractable.
