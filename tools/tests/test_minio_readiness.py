import sys
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_minio_tests import wait_for_storage


class StorageReadinessTest(unittest.TestCase):
    def setUp(self):
        self.server = MagicMock()
        self.server.poll.return_value = None
        self.response = MagicMock()
        self.response.__enter__.return_value.status = 200

    @patch("run_minio_tests.time.sleep")
    @patch("run_minio_tests.urllib.request.urlopen")
    def test_live_listener_does_not_hide_bucket_initialization_race(self, request, sleep):
        request.side_effect = [self.response, HTTPError("http://fixture", 503, "starting", {}, None),
                               self.response, self.response]
        wait_for_storage(self.server, "http://fixture")
        self.assertEqual(request.call_count, 4)
        self.assertEqual(request.call_args_list[0].args[0], "http://fixture/minio/health/cluster")
        self.assertEqual(request.call_args_list[1].args[0].get_method(), "PUT")
        sleep.assert_called_once()

    @patch("run_minio_tests.urllib.request.urlopen")
    def test_credentials_failure_is_not_retried(self, request):
        request.side_effect = [self.response, HTTPError("http://fixture", 403, "denied", {}, None)]
        with self.assertRaises(HTTPError):
            wait_for_storage(self.server, "http://fixture")
        self.assertEqual(request.call_count, 2)

    @patch("run_minio_tests.urllib.request.urlopen")
    def test_unready_storage_has_a_deadline(self, request):
        request.side_effect = HTTPError("http://fixture", 503, "starting", {}, None)
        with self.assertRaisesRegex(RuntimeError, "deadline exceeded"):
            wait_for_storage(self.server, "http://fixture", timeout=0)

    @patch("run_minio_tests.urllib.request.urlopen")
    def test_exited_process_fails_before_network_access(self, request):
        self.server.poll.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "exited before readiness"):
            wait_for_storage(self.server, "http://fixture")
        request.assert_not_called()


if __name__ == "__main__":
    unittest.main()
