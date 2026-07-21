import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent))

import unittest
from io import StringIO
from unittest.mock import MagicMock, patch

import main


class TestCosSkill(unittest.TestCase):
    def setUp(self):
        self._stdout_backup = sys.stdout
        self.capture_io = StringIO()
        sys.stdout = self.capture_io

    def tearDown(self):
        sys.stdout = self._stdout_backup

    def get_print_output(self):
        return self.capture_io.getvalue().strip()

    @patch("main.subprocess.check_call")
    @patch("main.CosS3Client")
    def test_upload_single_file(self, mock_cos_cli, mock_sub):
        mock_client = MagicMock()
        mock_cos_cli.return_value = mock_client
        args = ["main.py", "upload", "/tmp/demo.jpg", "cloud/demo.jpg"]
        with patch("sys.argv", args):
            with patch.object(Path, "exists", return_value=True):
                main.main()
        mock_client.put_object_from_local_file.assert_called_once()
        output = self.get_print_output()
        self.assertTrue(output.startswith("https://"))

    @patch("main.CosS3Client")
    def test_download_to_dir_auto_filename(self, mock_cos_cli):
        mock_client = MagicMock()
        mock_cos_cli.return_value = mock_client
        with patch.object(Path, "is_dir", return_value=True):
            args = ["main.py", "download", "cloud/demo.png", "/tmp/save/"]
            with patch("sys.argv", args):
                main.main()
        call_param = mock_client.download_file.call_args[1]["DestFilePath"]
        expect_path = str(Path("/tmp/save/demo.png"))
        self.assertEqual(call_param, expect_path)

    @patch("main.CosS3Client")
    def test_batch_download_dir(self, mock_cos_cli):
        mock_client = MagicMock()
        list_resp = {"Contents": [{"Key": "data/a.jpg"}], "IsTruncated": "false"}
        mock_client.list_objects.return_value = list_resp
        mock_cos_cli.return_value = mock_client

        args = ["main.py", "download-dir", "data/", "/tmp/local_save/"]
        with patch("sys.argv", args):
            main.main()
        output = self.get_print_output()
        expect_str = str(Path("/tmp/local_save/a.jpg"))
        self.assertIn(expect_str, output)

    @patch("main.CosS3Client")
    def test_delete_file(self, mock_cos_cli):
        mock_client = MagicMock()
        mock_cos_cli.return_value = mock_client
        args = ["main.py", "delete", "cloud/test.txt"]
        with patch("sys.argv", args):
            main.main()
        mock_client.delete_object.assert_called_once()

    @patch("main.CosS3Client")
    def test_list_dir_filter_subfolder(self, mock_cos_cli):
        mock_client = MagicMock()
        # 关键修复：所有文件key统一前缀 root/
        list_resp = {
            "Contents": [{"Key": "root/a.jpg"}, {"Key": "root/sub/b.jpg"}],
            "IsTruncated": "false",
        }
        mock_client.list_objects.return_value = list_resp
        mock_cos_cli.return_value = mock_client
        args = ["main.py", "list", "root/"]
        with patch("sys.argv", args):
            main.main()
        output = self.get_print_output()
        self.assertIn("root/a.jpg", output)
        self.assertNotIn("root/sub/b.jpg", output)

    def test_no_input_command(self):
        with patch("sys.argv", ["main.py"]):
            with self.assertRaises(Exception) as ctx:
                main.main()
        self.assertIn("缺少操作指令", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
