import os
import tempfile
import gc
import weakref

import numpy as np

import mkvcodec


def expect_value_error(callback) -> None:
    try:
        callback()
    except ValueError:
        return
    raise AssertionError("ValueError was not raised")


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "python.webm")
        width, height = 64, 48
        with mkvcodec.VideoWriter(
            path, fps=30, frame_size=(width, height), quality=32
        ) as writer:
            for index in range(30):
                rows, columns = np.indices((height, width))
                y = ((columns * 3 + rows * 2 + index * 7) & 0xFF).astype(np.uint8)
                u = np.full((height // 2, width // 2), 96 + index, np.uint8)
                v = np.full((height // 2, width // 2), 160 - index, np.uint8)
                writer.write((y, u, v))
            writer.flush()

        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            frames = []
            while True:
                frame = capture.read_i420()
                if frame is None:
                    break
                frames.append(frame)
        assert len(frames) == 30
        assert frames[0].y.shape == (height, width)
        assert frames[0].u.shape == (height // 2, width // 2)
        assert [frame.pts_ns for frame in frames] == sorted(
            frame.pts_ns for frame in frames
        )

        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            borrowed = capture.read_borrowed()
            assert borrowed is not None
            assert borrowed.pixel_format == "i420"
            assert borrowed.y.shape == (height, width)
            assert borrowed.u.shape == (height // 2, width // 2)
            assert borrowed.v.shape == (height // 2, width // 2)
            assert not borrowed.y.flags.writeable
            retained_y = borrowed.y[:, 1:]
            retained_u = borrowed.u
            retained_v = borrowed.v
            retained_pts = borrowed.pts_ns
            try:
                borrowed.y[0, 0] = 0
            except ValueError:
                pass
            else:
                raise AssertionError("borrowed decode plane must be read-only")
            borrowed.close()
            try:
                _ = borrowed.y
            except RuntimeError:
                pass
            else:
                raise AssertionError("closed borrowed frame remained accessible")
        # ndarray views retain the native frame independently of Capture/frame.
        assert retained_y.shape == (height, width - 1)
        assert int(retained_y[0, 0]) >= 0

        borrowed_path = os.path.join(directory, "borrowed.webm")
        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            borrowed = capture.read_borrowed()
            assert borrowed is not None
            borrowed_planes = borrowed.planes
        with mkvcodec.VideoWriter(
            borrowed_path, fps=30, frame_size=(width, height), queue_size=0
        ) as writer:
            writer.write_borrowed(
                borrowed_planes, format="i420", pts=borrowed.pts_ns
            )
        borrowed.close()
        del borrowed_planes
        del retained_y, retained_u, retained_v
        gc.collect()
        with mkvcodec.VideoCapture(borrowed_path, prefetch=0) as capture:
            decoded_borrowed = capture.read_i420()
            assert decoded_borrowed is not None
            assert decoded_borrowed.pts_ns == retained_pts
            assert capture.read_i420() is None

        with mkvcodec.VideoWriter(
            os.path.join(directory, "borrowed-invalid.webm"),
            fps=30, frame_size=(width, height), queue_size=1,
        ) as writer:
            expect_value_error(
                lambda: writer.write_borrowed(
                    (frames[0].y, frames[0].u, frames[0].v), format="i420"
                )
            )
            writer.write((frames[0].y, frames[0].u, frames[0].v))

        async_borrowed_path = os.path.join(directory, "borrowed-async.webm")
        async_y = np.full((height, width), 96, np.uint8)
        async_u = np.full((height // 2, width // 2), 128, np.uint8)
        async_v = np.full((height // 2, width // 2), 128, np.uint8)
        async_y_ref = weakref.ref(async_y)
        with mkvcodec.VideoWriter(
            async_borrowed_path, fps=30, frame_size=(width, height), queue_size=1,
        ) as writer:
            submission = writer.submit_borrowed(
                (async_y, async_u, async_v), format="i420", pts=0
            )
            del async_y, async_u, async_v
            gc.collect()
            assert async_y_ref() is not None
            submission.wait(5000)
            assert submission.done
            gc.collect()
            assert async_y_ref() is None
            submission.close()
        with mkvcodec.VideoCapture(async_borrowed_path, prefetch=0) as capture:
            assert capture.read_i420() is not None
            assert capture.read_i420() is None

        canceled_path = os.path.join(directory, "canceled.webm")
        os.environ["MKVC_TEST_ENCODER_DELAY_MS"] = "250"
        try:
            with mkvcodec.VideoWriter(
                canceled_path, fps=30, frame_size=(width, height), queue_size=2,
            ) as writer:
                canceled_submissions = [
                    writer.submit_borrowed(
                        (frames[0].y, frames[0].u, frames[0].v),
                        format="i420", pts=index,
                    )
                    for index in range(3)
                ]
                writer.cancel()
                try:
                    writer.write((frames[0].y, frames[0].u, frames[0].v))
                except RuntimeError:
                    pass
                else:
                    raise AssertionError("write after cancel did not fail")
                canceled_count = 0
                for submission in canceled_submissions:
                    try:
                        submission.wait(5000)
                    except RuntimeError:
                        canceled_count += 1
                    assert submission.done
                    submission.close()
                assert canceled_count > 0
        finally:
            os.environ.pop("MKVC_TEST_ENCODER_DELAY_MS", None)

        pool_path = os.path.join(directory, "native-pool.webm")
        pool = mkvcodec.CpuFramePool("i420", (width, height), capacity=1)
        buffer = pool.acquire()
        first_generation = buffer.generation
        y, u, v = buffer.planes
        assert y.flags.writeable
        y[:] = 96
        u[:] = 128
        v[:] = 128
        assert pool.try_acquire() is None
        with mkvcodec.VideoWriter(
            pool_path, fps=30, frame_size=(width, height), queue_size=1,
        ) as writer:
            submission = writer.submit_buffer(buffer, pts=0)
            buffer.close()
            del y, u, v
            gc.collect()
            submission.wait(5000)
            submission.close()
        recycled = pool.acquire(5000)
        assert recycled.generation > first_generation
        retained = recycled.planes[0][:, 1:]
        recycled.close()
        gc.collect()
        assert pool.try_acquire() is None
        del retained
        gc.collect()
        after_view = pool.acquire(5000)
        after_view.close()
        del after_view
        gc.collect()
        survivor = pool.acquire()
        survivor_y = survivor.planes[0]
        pool.close()
        assert survivor_y.shape == (height, width)
        survivor.close()
        del survivor_y
        gc.collect()
        with mkvcodec.VideoCapture(pool_path, prefetch=0) as capture:
            assert capture.read_i420() is not None
            assert capture.read_i420() is None

        pool_layouts = {
            "nv12": ((height, width), (height // 2, width)),
            "bgr": ((height, width, 3),),
            "rgb": ((height, width, 3),),
            "bgra": ((height, width, 4),),
        }
        for format_name, expected_shapes in pool_layouts.items():
            with mkvcodec.CpuFramePool(
                format_name, (width, height), capacity=1
            ) as layout_pool:
                with layout_pool.acquire() as layout_buffer:
                    assert layout_buffer.format == format_name
                    assert tuple(
                        plane.shape for plane in layout_buffer.planes
                    ) == expected_shapes
                    assert all(
                        plane.flags.writeable for plane in layout_buffer.planes
                    )

        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            processed = capture.read_processed(
                crop=(8, 4, 48, 40),
                size=(80, 80),
                fit="contain",
                rotate=90,
                flip_horizontal=True,
                background=(0, 0, 0),
                format="bgr",
            )
            assert processed is not None
            assert processed.shape == (80, 80, 3)
            assert capture.last_pts_ns == 0
        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            covered = capture.read_processed(size=(32, 32), fit="cover", format="i420")
            assert covered is not None
            assert covered.y.shape == (32, 32)
            assert covered.u.shape == (16, 16)
        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            processed_nv12 = capture.read_processed(
                size=(32, 24), flip_vertical=True, format="nv12"
            )
            assert processed_nv12 is not None
            assert processed_nv12[0].shape == (24, 32)
            assert processed_nv12[1].shape == (12, 32)

        nonblocking_path = os.path.join(directory, "nonblocking.webm")
        image = np.zeros((height, width, 3), np.uint8)
        accepted = 0
        with mkvcodec.VideoWriter(
            nonblocking_path, fps=30, frame_size=(width, height), queue_size=1
        ) as writer:
            for index in range(100):
                image.fill(index)
                if writer.try_write(image):
                    accepted += 1
            writer.flush()
            writer.write(image)
            accepted += 1
        with mkvcodec.VideoCapture(nonblocking_path) as capture:
            assert sum(1 for _ in capture) == accepted
        assert accepted >= 2
        with mkvcodec.VideoCapture(path, prefetch=4) as capture:
            prefetched = list(capture)
        assert len(prefetched) == 30
        assert prefetched[0].shape == (height, width, 3)
        capture = mkvcodec.VideoCapture(path, prefetch=1)
        assert capture.read_bgr() is not None
        capture.close()
        capture.close()

        expected_blue_y = 41
        packed_cases = {
            "bgr": (np.full((height, width + 4, 3), (255, 0, 0), np.uint8)[:, :width],
                    "write_bgr"),
            "rgb": (np.full((height, width, 3), (0, 0, 255), np.uint8),
                    "write_rgb"),
            "bgra": (np.full((height, width, 4), (255, 0, 0, 255), np.uint8),
                     "write_bgra"),
        }
        for name, (image, method_name) in packed_cases.items():
            packed_path = os.path.join(directory, f"{name}.webm")
            with mkvcodec.VideoWriter(
                packed_path, fps=30, frame_size=(width, height)
            ) as writer:
                if name == "bgr":
                    expect_value_error(
                        lambda: writer.write_bgr(image.astype(np.float32))
                    )
                    expect_value_error(
                        lambda: writer.write_bgr(image[:, :-1])
                    )
                    expect_value_error(
                        lambda: writer.write_bgr(image[::-1])
                    )
                getattr(writer, method_name)(image)
            with mkvcodec.VideoCapture(packed_path) as capture:
                decoded = capture.read_i420()
                assert decoded is not None
                assert capture.read_i420() is None
            assert abs(float(decoded.y.mean()) - expected_blue_y) <= 5

            if name == "bgr":
                with mkvcodec.VideoCapture(packed_path) as capture:
                    bgr = capture.read_bgr()
                    assert bgr is not None
                    assert bgr.shape == (height, width, 3)
                    assert float(bgr[..., 0].mean()) > 240
                    assert float(bgr[..., 2].mean()) < 15
                    assert capture.last_pts_ns == 0
                with mkvcodec.VideoCapture(packed_path) as capture:
                    rgb = capture.read_rgb()
                    assert rgb is not None
                    assert float(rgb[..., 2].mean()) > 240
                    assert float(rgb[..., 0].mean()) < 15
                with mkvcodec.VideoCapture(packed_path) as capture:
                    bgra = capture.read_bgra()
                    assert bgra is not None
                    assert bgra.shape == (height, width, 4)
                    assert np.all(bgra[..., 3] == 255)
                with mkvcodec.VideoCapture(packed_path) as capture:
                    nv12 = capture.read_nv12()
                    assert nv12 is not None
                    assert nv12[0].shape == (height, width)
                    assert nv12[1].shape == (height // 2, width)

        nv12_path = os.path.join(directory, "nv12.webm")
        nv12_y = np.full((height, width), expected_blue_y, np.uint8)
        nv12_uv = np.empty((height // 2, width), np.uint8)
        nv12_uv[:, 0::2] = 240
        nv12_uv[:, 1::2] = 110
        with mkvcodec.VideoWriter(
            nv12_path, fps=30, frame_size=(width, height)
        ) as writer:
            writer.write_nv12(nv12_y, nv12_uv)
        with mkvcodec.VideoCapture(nv12_path) as capture:
            decoded = capture.read_i420()
        assert decoded is not None
        assert abs(float(decoded.y.mean()) - expected_blue_y) <= 5

        long_path = os.path.join(directory, "long.webm")
        y = np.full((height, width), 96, np.uint8)
        u = np.full((height // 2, width // 2), 128, np.uint8)
        v = np.full((height // 2, width // 2), 128, np.uint8)
        with mkvcodec.VideoWriter(
            long_path, fps=30, frame_size=(width, height), quality=40
        ) as writer:
            for index in range(1000):
                y[0, 0] = index & 0xFF
                writer.write((y, u, v))
        with mkvcodec.VideoCapture(long_path) as capture:
            decoded_count = sum(1 for _ in capture)
        assert decoded_count == 1000


if __name__ == "__main__":
    main()
