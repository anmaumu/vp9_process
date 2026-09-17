#!/usr/bin/env bash
set -euo pipefail
shopt -s nullglob

source_root=/io
output_root=/out
cache_root=/cache
work_root=/tmp/mkvcodec-manylinux
python_root=/opt/python/cp39-cp39
python_bin="${python_root}/bin/python"
vcpkg_root="${cache_root}/vcpkg"
vcpkg_commit=114d9fe62faf35856b45cf55cb93b57028a45d63

if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "manylinux x86_64 build requires an x86_64 container" >&2
    exit 2
fi
if [[ ! -x "${python_bin}" ]]; then
    echo "CPython 3.9 is absent from the selected manylinux image" >&2
    exit 2
fi
if [[ -e "${output_root}/raw" || -e "${output_root}/repaired" ]]; then
    echo "output directory is not empty; use a fresh --output path" >&2
    exit 2
fi
if [[ "${MKVC_QUALIFICATION_ONLY:-0}" != "1" && ! -s "${source_root}/LICENSE" ]]; then
    echo "formal manylinux build requires a non-empty repository LICENSE" >&2
    exit 2
fi

dnf install -y \
    autoconf automake bison curl diffutils flex git libtool make nasm \
    ninja-build patch perl pkgconf-pkg-config tar unzip zip >/dev/null

mkdir -p "${cache_root}/archives" "${work_root}" "${output_root}/raw" \
    "${output_root}/repaired"
export VCPKG_DEFAULT_BINARY_CACHE="${cache_root}/archives"
if [[ ! -d "${vcpkg_root}/.git" ]]; then
    git clone --filter=blob:none https://github.com/microsoft/vcpkg.git \
        "${vcpkg_root}"
fi
git -C "${vcpkg_root}" fetch --quiet origin "${vcpkg_commit}"
git -C "${vcpkg_root}" checkout --quiet --detach "${vcpkg_commit}"
"${vcpkg_root}/bootstrap-vcpkg.sh" -disableMetrics >/dev/null

cmake -S "${source_root}" -B "${work_root}/build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${vcpkg_root}/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux-release \
    -DVCPKG_OVERLAY_TRIPLETS="${source_root}/packaging/manylinux/triplets" \
    -DVCPKG_INSTALLED_DIR="${work_root}/vcpkg_installed" \
    -DVCPKG_INSTALL_OPTIONS=--allow-unsupported \
    -DPython3_ROOT_DIR="${python_root}" \
    -DPython3_EXECUTABLE="${python_bin}" \
    -DMKVC_BUILD_TESTS=OFF \
    -DMKVC_BUILD_PYTHON_DLPACK=ON \
    -DMKVC_ENABLE_CPU_VP9=ON \
    -DMKVC_ENABLE_CPU_AV1=ON \
    -DMKVC_ENABLE_INTEL_ONEVPL=ON \
    -DMKVC_ENABLE_NVIDIA=ON
cmake --build "${work_root}/build" --target mkvcodec mkvc_python_dlpack --parallel

"${python_bin}" "${source_root}/tools/collect_licenses.py" \
    --vcpkg-root "${vcpkg_root}" \
    --nvcodec-include \
        "${work_root}/build/_deps/nv_codec_headers-src/include/ffnvcodec" \
    --exclude-component Highway \
    --output "${work_root}/legal"

qualification_arguments=()
if [[ "${MKVC_QUALIFICATION_ONLY:-0}" == "1" ]]; then
    project_license="${work_root}/PROJECT_LICENSE.txt"
    printf '%s\n\n%s\n' \
        'NOT A PROJECT LICENSE.' \
        'Qualification fixture only; publication and redistribution are prohibited.' \
        >"${project_license}"
    qualification_arguments+=(--qualification-only)
else
    project_license="${source_root}/LICENSE"
    if [[ ! -s "${project_license}" ]]; then
        echo "formal manylinux build requires a non-empty repository LICENSE" >&2
        exit 2
    fi
fi

"${python_bin}" "${source_root}/tools/build_wheel.py" \
    --native "${work_root}/build/libmkvcodec.so" \
    --dlpack-extension "${work_root}/build/_dlpack.abi3.so" \
    --legal-dir "${work_root}/legal" \
    --project-license "${project_license}" \
    --output-dir "${output_root}/raw" \
    --platform-tag linux_x86_64 \
    --exclude-component Highway \
    "${qualification_arguments[@]}"

raw_wheel=("${output_root}/raw"/*.whl)
if [[ ${#raw_wheel[@]} -ne 1 ]]; then
    echo "expected exactly one raw wheel" >&2
    exit 2
fi
auditwheel show "${raw_wheel[0]}" | tee "${output_root}/auditwheel-before.txt"
auditwheel repair --plat manylinux_2_28_x86_64 \
    --wheel-dir "${output_root}/repaired" "${raw_wheel[0]}"
repaired_wheel=("${output_root}/repaired"/*.whl)
if [[ ${#repaired_wheel[@]} -ne 1 ]]; then
    echo "expected exactly one repaired wheel" >&2
    exit 2
fi
auditwheel show "${repaired_wheel[0]}" | tee "${output_root}/auditwheel-after.txt"

"${python_bin}" -m pip install --disable-pip-version-check --no-deps \
    --root-user-action ignore --target "${work_root}/installed" \
    numpy==2.0.2 "${repaired_wheel[0]}" >/dev/null
PYTHONPATH="${work_root}/installed" "${python_bin}" -c \
    'import mkvcodec; print(mkvcodec.backend_capabilities())' \
    | tee "${output_root}/isolated-import.txt"

if [[ -n "${MKVC_HOST_UID:-}" && -n "${MKVC_HOST_GID:-}" ]]; then
    chown -R "${MKVC_HOST_UID}:${MKVC_HOST_GID}" "${output_root}"
fi
