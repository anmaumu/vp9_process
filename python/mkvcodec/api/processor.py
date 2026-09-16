"""Backend-neutral GPU image-processing adapter contract.

The codec package owns frame lifetime and synchronization but intentionally
does not link NPP, SYCL, OpenCL, or Direct3D kernels into the native core.
Adapters implement those vendor operations behind one application-facing API.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Iterable, Protocol, runtime_checkable


class GpuProcessingUnavailableError(RuntimeError):
    """Raised when no GPU-only adapter can satisfy a conversion request."""


@dataclass(frozen=True)
class GpuConversionRequest:
    """Describe one packed GPU image conversion.

    Attributes
    ----------
    format : {"rgb", "bgr", "rgba", "bgra"}
        Requested channel order.
    layout : {"hwc", "chw"}
        Requested tensor dimension order.
    dtype : {"uint8", "float16", "float32"}
        Requested element representation.
    color_space : {"auto", "bt601", "bt709", "bt2020"}
        Matrix coefficients used for YUV conversion. ``"auto"`` asks the
        adapter to use source metadata or a documented backend default.
    color_range : {"auto", "limited", "full"}
        Source/output range interpretation.
    """

    format: str = "rgb"
    layout: str = "hwc"
    dtype: str = "uint8"
    color_space: str = "auto"
    color_range: str = "auto"

    def __post_init__(self) -> None:
        if self.format not in ("rgb", "bgr", "rgba", "bgra"):
            raise ValueError("GPU output format must be rgb, bgr, rgba, or bgra")
        if self.layout not in ("hwc", "chw"):
            raise ValueError("GPU output layout must be hwc or chw")
        if self.dtype not in ("uint8", "float16", "float32"):
            raise ValueError("GPU output dtype must be uint8, float16, or float32")
        if self.color_space not in ("auto", "bt601", "bt709", "bt2020"):
            raise ValueError("GPU color_space must be auto, bt601, bt709, or bt2020")
        if self.color_range not in ("auto", "limited", "full"):
            raise ValueError("GPU color_range must be auto, limited, or full")


@dataclass(frozen=True)
class GpuImageInfo:
    """Immutable description of an externally processed GPU image."""

    backend: str
    device_id: int
    width: int
    height: int
    format: str
    layout: str
    dtype: str
    shape: tuple[int, ...]
    color_space: str
    color_range: str
    pts_ns: int
    adapter: str
    completion: str
    copy_path: str


class GpuImage:
    """Own a vendor-processed GPU image exposed through DLPack.

    Parameters
    ----------
    provider : object
        Object implementing ``__dlpack__`` and ``__dlpack_device__``.
    info : GpuImageInfo
        Backend-neutral image metadata.
    wait : callable, optional
        Function accepting a millisecond timeout and waiting for producer
        completion. DLPack stream dependency remains the preferred async path.
    release : callable, optional
        Releases the adapter's output lease. It is invoked exactly once.
    owners : iterable of object, optional
        Additional owners retained until :meth:`close`. Objects with a
        ``close`` method are closed in reverse order.

    Notes
    -----
    A DLPack consumer receives its own managed-tensor lease. Closing this
    object after successful consumption must therefore remain safe; adapters
    are responsible for implementing that standard ownership contract.
    """

    def __init__(
        self,
        provider: object,
        info: GpuImageInfo,
        *,
        wait: Callable[[int], None] | None = None,
        release: Callable[[], None] | None = None,
        owners: Iterable[object] = (),
        wait_before_dlpack: bool = False,
    ) -> None:
        if not callable(getattr(provider, "__dlpack__", None)):
            raise TypeError("GPU image provider must implement __dlpack__")
        if not callable(getattr(provider, "__dlpack_device__", None)):
            raise TypeError("GPU image provider must implement __dlpack_device__")
        self._provider: object | None = provider
        self._wait = wait
        self._release = release
        self._owners = list(owners)
        self._wait_before_dlpack = wait_before_dlpack
        self._closed = False
        self.info = info

    @property
    def closed(self) -> bool:
        """bool: Whether this Python output lease has been released."""
        return self._closed

    @property
    def backend(self) -> str:
        """str: GPU backend that owns the output allocation."""
        return self.info.backend

    @property
    def device_id(self) -> int:
        """int: Backend-local device identifier."""
        return self.info.device_id

    @property
    def format(self) -> str:
        """str: Packed channel order such as ``"rgb"`` or ``"bgra"``."""
        return self.info.format

    @property
    def layout(self) -> str:
        """str: Tensor layout, either ``"hwc"`` or ``"chw"``."""
        return self.info.layout

    @property
    def dtype(self) -> str:
        """str: Element type of the output tensor."""
        return self.info.dtype

    @property
    def shape(self) -> tuple[int, ...]:
        """tuple of int: Output tensor dimensions."""
        return self.info.shape

    @property
    def pts_ns(self) -> int:
        """int: Source presentation timestamp in nanoseconds."""
        return self.info.pts_ns

    def __dlpack_device__(self) -> tuple[int, int]:
        """Return the standard DLPack device tuple."""
        if self._closed or self._provider is None:
            raise RuntimeError("GPU image is released")
        value = self._provider.__dlpack_device__()
        return int(value[0]), int(value[1])

    def __dlpack__(
        self,
        *,
        stream: int | None = None,
        max_version: tuple[int, int] | None = None,
        dl_device: tuple[int, int] | None = None,
        copy: bool | None = None,
    ) -> object:
        """Export the processed allocation through the DLPack protocol."""
        if self._closed or self._provider is None:
            raise RuntimeError("GPU image is released")
        if copy:
            raise BufferError("GpuImage DLPack export does not permit implicit copies")
        if self._wait_before_dlpack and self._wait is not None:
            self._wait(0xFFFFFFFF)
        arguments: dict[str, object] = {}
        if stream is not None:
            arguments["stream"] = stream
        if max_version is not None:
            arguments["max_version"] = max_version
        if dl_device is not None:
            arguments["dl_device"] = dl_device
        if copy is not None:
            arguments["copy"] = copy
        return self._provider.__dlpack__(**arguments)

    def wait(self, timeout_ms: int = 0xFFFFFFFF) -> None:
        """Wait for adapter producer completion when an explicit wait exists."""
        if self._closed:
            raise RuntimeError("GPU image is released")
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        if self._wait is not None:
            self._wait(timeout_ms)

    def _attach_owner(self, owner: object) -> None:
        """Attach an internal source lease after adapter validation."""
        if self._closed:
            close = getattr(owner, "close", None)
            if callable(close):
                close()
            raise RuntimeError("GPU image is released")
        self._owners.append(owner)

    def close(self) -> None:
        """Release this output and its retained source leases exactly once."""
        if self._closed:
            return
        self._closed = True
        failure: BaseException | None = None
        try:
            if self._release is not None:
                self._release()
        except BaseException as exception:
            failure = exception
        finally:
            self._release = None
            self._wait = None
            self._provider = None
            self._wait_before_dlpack = False
            for owner in reversed(self._owners):
                close = getattr(owner, "close", None)
                if callable(close):
                    try:
                        close()
                    except BaseException as exception:
                        if failure is None:
                            failure = exception
            self._owners.clear()
        if failure is not None:
            raise failure

    release = close

    def __enter__(self) -> "GpuImage":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def __del__(self) -> None:
        if getattr(self, "_closed", True) is False:
            try:
                self.close()
            except Exception:
                pass


@runtime_checkable
class GpuProcessorAdapter(Protocol):
    """Protocol implemented by optional NVIDIA and Intel processors."""

    name: str
    backends: tuple[str, ...]

    def supports(self, source: object, request: GpuConversionRequest) -> bool:
        """Return whether this adapter can perform the requested conversion."""

    def convert(self, source: object, request: GpuConversionRequest) -> GpuImage:
        """Schedule GPU conversion and return an owning output lease."""


class GpuProcessor:
    """Select vendor adapters behind one GPU conversion API.

    Parameters
    ----------
    backend : {"auto", "intel", "nvidia"}, default: "auto"
        Restrict selection or infer it from each source frame.
    adapters : iterable of GpuProcessorAdapter, optional
        Installed adapters. Omission discovers adapters whose optional runtime
        dependencies are importable. Pass an explicit iterable for deterministic
        application-controlled selection.
    """

    def __init__(
        self,
        *,
        backend: str = "auto",
        adapters: Iterable[GpuProcessorAdapter] | None = None,
    ) -> None:
        if backend not in ("auto", "intel", "nvidia"):
            raise ValueError("GPU processor backend must be auto, intel, or nvidia")
        self.backend = backend
        self._adapters = self._default_adapters() if adapters is None else tuple(adapters)
        for adapter in self._adapters:
            if not isinstance(adapter, GpuProcessorAdapter):
                raise TypeError("GPU processor adapter does not implement the required protocol")

    @property
    def adapters(self) -> tuple[str, ...]:
        """tuple of str: Registered adapter names in selection order."""
        return tuple(adapter.name for adapter in self._adapters)

    @staticmethod
    def _default_adapters() -> tuple[GpuProcessorAdapter, ...]:
        """Instantiate optional built-in adapters without hard dependencies."""
        discovered: list[GpuProcessorAdapter] = []
        try:
            from ..interop.cupy_processor import NvidiaCupyProcessorAdapter

            discovered.append(NvidiaCupyProcessorAdapter())
        except (ImportError, OSError):
            pass
        try:
            from ..interop.intel_opencl_processor import IntelOpenClProcessorAdapter

            discovered.append(IntelOpenClProcessorAdapter())
        except (ImportError, OSError, RuntimeError):
            pass
        try:
            from ..interop.dpnp_processor import IntelDpnpProcessorAdapter

            discovered.append(IntelDpnpProcessorAdapter())
        except (ImportError, OSError):
            pass
        return tuple(discovered)

    def close(self) -> None:
        """Close adapters that own persistent GPU contexts or output pools."""
        failure: BaseException | None = None
        for adapter in reversed(self._adapters):
            close = getattr(adapter, "close", None)
            if callable(close):
                try:
                    close()
                except BaseException as exception:
                    if failure is None:
                        failure = exception
        self._adapters = ()
        if failure is not None:
            raise failure

    def __enter__(self) -> "GpuProcessor":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def convert(
        self,
        source: object,
        *,
        format: str = "rgb",
        layout: str = "hwc",
        dtype: str = "uint8",
        color_space: str = "auto",
        color_range: str = "auto",
    ) -> GpuImage:
        """Convert one decoded surface without permitting a CPU fallback.

        Returns
        -------
        GpuImage
            DLPack-capable output whose lifetime retains the decoded source.

        Raises
        ------
        GpuProcessingUnavailableError
            If no registered adapter supports the source and request.
        """
        request = GpuConversionRequest(
            format=format,
            layout=layout,
            dtype=dtype,
            color_space=color_space,
            color_range=color_range,
        )
        interop = source.interop
        source_backend = str(interop.backend)
        if source_backend not in ("intel", "nvidia"):
            raise GpuProcessingUnavailableError(f"unsupported GPU source backend: {source_backend}")
        if self.backend != "auto" and self.backend != source_backend:
            raise GpuProcessingUnavailableError(
                f"{self.backend} processor cannot consume a {source_backend} frame"
            )

        selected = None
        for adapter in self._adapters:
            if source_backend not in adapter.backends:
                continue
            if adapter.supports(source, request):
                selected = adapter
                break
        if selected is None:
            names = ", ".join(self.adapters) or "none"
            raise GpuProcessingUnavailableError(
                f"no GPU-only adapter supports {source_backend} {request.format}/"
                f"{request.layout}/{request.dtype}; registered adapters: {names}"
            )

        retained = source.retain()
        try:
            output = selected.convert(source, request)
            if not isinstance(output, GpuImage):
                raise TypeError("GPU processor adapter must return GpuImage")
            output._attach_owner(retained)
            retained = None
            self._validate_output(source, request, selected.name, output)
            return output
        except BaseException:
            if retained is not None:
                try:
                    retained.close()
                except BaseException:
                    pass
            elif "output" in locals() and isinstance(output, GpuImage):
                try:
                    output.close()
                except BaseException:
                    pass
            raise

    @staticmethod
    def _validate_output(
        source: object,
        request: GpuConversionRequest,
        adapter_name: str,
        output: GpuImage,
    ) -> None:
        """Reject adapter results that violate the common GPU contract."""
        descriptor = source.descriptor
        info = output.info
        channels = 3 if request.format in ("rgb", "bgr") else 4
        width = int(descriptor["width"])
        height = int(descriptor["height"])
        shape = (height, width, channels) if request.layout == "hwc" else (channels, height, width)
        expected = (
            str(source.interop.backend),
            int(descriptor["device_id"]),
            width,
            height,
            request.format,
            request.layout,
            request.dtype,
            shape,
            int(descriptor.get("pts_ns", -1)),
            adapter_name,
        )
        observed = (
            info.backend,
            info.device_id,
            info.width,
            info.height,
            info.format,
            info.layout,
            info.dtype,
            info.shape,
            info.pts_ns,
            info.adapter,
        )
        if observed != expected:
            raise ValueError("GPU processor adapter returned mismatched image metadata")
        if info.color_space not in ("bt601", "bt709", "bt2020"):
            raise ValueError("GPU processor adapter returned invalid effective color space")
        if request.color_space != "auto" and info.color_space != request.color_space:
            raise ValueError("GPU processor adapter changed the requested color space")
        if info.color_range not in ("limited", "full"):
            raise ValueError("GPU processor adapter returned invalid effective color range")
        if request.color_range != "auto" and info.color_range != request.color_range:
            raise ValueError("GPU processor adapter changed the requested color range")
        if info.copy_path != "gpu_copy":
            raise ValueError("packed GPU color conversion must report copy_path='gpu_copy'")


__all__ = [
    "GpuConversionRequest",
    "GpuImage",
    "GpuImageInfo",
    "GpuProcessingUnavailableError",
    "GpuProcessor",
    "GpuProcessorAdapter",
]
