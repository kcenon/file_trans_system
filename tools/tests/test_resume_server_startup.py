"""Keep initial fixture port retries distinct from endpoint-preserving recovery."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location(
    "resume_scenario", Path(__file__).resolve().parents[2] / "tests/integration/test_process_resume.py")
scenario = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scenario)


class ResumeServerStartupTest(unittest.TestCase):
    def test_initial_conflict_can_choose_another_port(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (root / "server.log").open("w") as log:
                failed, ready = Mock(), Mock()
                failed.poll.return_value, ready.poll.return_value = 1, None

                def launch(command, **kwargs):
                    if command[3] == "1234":
                        log.write("Failed to bind to port: address already in use\n")
                        log.flush()
                        return failed
                    (root / "server-ready").write_text("ready")
                    return ready

                with patch.object(scenario.subprocess, "Popen", side_effect=launch) as popen, \
                        patch.object(scenario, "free_port", return_value=5678):
                    process, port = scenario.start_server("fixture", root, 1234, "plain", log,
                                                          retry_port_conflicts=True)
                    self.assertIs(process, ready)
                    self.assertEqual(port, 5678)
                    self.assertEqual(popen.call_count, 2)

    def check_failure(self, output, allow_retry, expected_attempts):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (root / "server.log").open("w") as log:
                failed = Mock()
                failed.poll.return_value = 1

                def launch(*args, **kwargs):
                    log.write(output)
                    log.flush()
                    return failed

                with patch.object(scenario.subprocess, "Popen", side_effect=launch) as popen, \
                        patch.object(scenario, "free_port", return_value=5678) as choose:
                    with self.assertRaisesRegex(AssertionError, "Server failed to start"):
                        scenario.start_server("fixture", root, 1234, "plain", log,
                                              retry_port_conflicts=allow_retry)
                    self.assertEqual(popen.call_count, expected_attempts)
                    self.assertEqual(choose.call_count, expected_attempts - 1)

    def test_restart_never_changes_the_checkpoint_endpoint(self):
        self.check_failure("address already in use", False, 1)

    def test_other_startup_errors_are_not_retried(self):
        self.check_failure("invalid encryption configuration", True, 1)

    def test_initial_allocation_is_bounded(self):
        self.check_failure("address already in use", True, 5)


if __name__ == "__main__":
    unittest.main()
