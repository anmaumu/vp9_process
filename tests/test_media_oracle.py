"""Guard against terminal-dependent hangs in file-based CPU oracles."""
import subprocess
import unittest
from unittest.mock import patch
from media_oracle import run_oracle


class OracleTests(unittest.TestCase):
    def test_ffmpeg_stdin_disabled_without_mutating_input(self):
        command = ["ffmpeg", "-i", "sample.webm", "-f", "null", "-"]
        with patch("media_oracle.subprocess.run") as run:
            self.assertIs(run_oracle(command), run.return_value)
            run.assert_called_once_with(["ffmpeg", "-nostdin", *command[1:]],
                                        stdin=subprocess.DEVNULL, check=True,
                                        capture_output=True, timeout=60)
        self.assertNotIn("-nostdin", command)

    def test_ffprobe_has_no_ffmpeg_only_option(self):
        command = ["ffprobe", "-of", "json", "sample.webm"]
        with patch("media_oracle.subprocess.run") as run:
            run_oracle(command)
            run.assert_called_once_with(command, stdin=subprocess.DEVNULL,
                                        check=True, capture_output=True, timeout=60)

    def test_failure_and_timeout_not_swallowed(self):
        for error in (subprocess.TimeoutExpired("ffmpeg", 60),
                      subprocess.CalledProcessError(1, "ffmpeg")):
            with patch("media_oracle.subprocess.run", side_effect=error), self.assertRaises(type(error)):
                run_oracle(["ffmpeg", "-i", "sample.webm"])


if __name__ == "__main__":
    unittest.main()
