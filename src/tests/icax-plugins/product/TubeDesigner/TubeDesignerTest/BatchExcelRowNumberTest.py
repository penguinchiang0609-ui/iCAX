"""Physical Excel row regressions against a real TubeDesigner SDO stdio host.

Run with --bridge <acceptance-host.exe> --runtime <Release directory>
--output <new fixture directory>. Workbooks are exported by the loaded DLL;
only worksheet data rows are changed using standard ZIP/XML containers.
"""
import argparse
import html
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import threading
import unittest
import zipfile


class NativeHost:
    def __init__(self, executable, runtime):
        env = dict(os.environ)
        env["PATH"] = str(runtime) + ";" + env.get("PATH", "")
        self.process = subprocess.Popen(
            [str(executable)], cwd=runtime, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8")
        self.responses = queue.Queue()
        self.calls = []
        self.extra_output = []
        self.errors = []

        def read_stdout():
            for line in self.process.stdout:
                try:
                    self.responses.put(json.loads(line))
                except json.JSONDecodeError:
                    self.extra_output.append(line.rstrip())
            self.responses.put(None)

        def read_stderr():
            self.errors.extend(self.process.stderr.read().splitlines())

        threading.Thread(target=read_stdout, daemon=True).start()
        threading.Thread(target=read_stderr, daemon=True).start()

    def call(self, method, payload):
        request = {"id": len(self.calls) + 1, "method": method, "payload": payload}
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        response = self.responses.get(timeout=60)
        if response is None:
            raise RuntimeError("Native host ended without a response: " + repr(self.errors))
        if response["id"] != request["id"]:
            raise RuntimeError("Native host response identity differs")
        self.calls.append({"request": request, "response": response})
        return response

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=30)
        if self.process.returncode != 0:
            raise RuntimeError("Native host failed: " + repr(self.errors))


def cell(column, row, value):
    if value == "":
        return ""
    return (f'<c r="{column}{row}" t="inlineStr"><is><t xml:space="preserve">'
            + html.escape(str(value)) + "</t></is></c>")


class BatchExcelPhysicalRowsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output.mkdir(parents=True, exist_ok=False)
        cls.host = NativeHost(cls.bridge, cls.runtime)
        modules = cls.host.call("GetRuntimeModules", {})
        if not modules["ok"]:
            raise RuntimeError(modules)
        for name, path in modules["result"]["modules"].items():
            if Path(path).resolve() != (cls.runtime / name).resolve():
                raise RuntimeError("Native host loaded a module outside the tested runtime")
        cls.source = cls.output / "native-template.xlsx"
        response = cls.host.call("ExportBatchExcelTemplate", {
            "templateId": "minimal-protective-grille", "targetPath": str(cls.source),
            "columns": [
                {"key": "__instanceName", "title": "实例名称", "required": True},
                {"key": "__instanceQuantity", "title": "生产数量", "defaultValue": "1"},
                {"key": "width", "title": "宽度", "required": True},
            ],
        })
        if not response["ok"]:
            raise RuntimeError(response)

    @classmethod
    def tearDownClass(cls):
        (cls.output / "calls.json").write_text(
            json.dumps(cls.host.calls, ensure_ascii=False, indent=2), encoding="utf-8")
        cls.host.close()

    def workbook(self, name, rows, header_row=2):
        target = self.output / (name + ".xlsx")
        with zipfile.ZipFile(self.source) as source, zipfile.ZipFile(
                target, "w", compression=zipfile.ZIP_DEFLATED) as result:
            for item in source.infolist():
                data = source.read(item.filename)
                if item.filename == "xl/worksheets/sheet1.xml":
                    xml = data.decode("utf-8")
                    xml = xml.replace('<row r="2"', f'<row r="{header_row}"')
                    xml = xml.replace('</sheetData>', ''.join(rows) + '</sheetData>')
                    xml = re.sub(r'<dimension ref="[^"]+"/>', '<dimension ref="A1:C1048576"/>', xml)
                    data = xml.encode("utf-8")
                result.writestr(item.filename, data)
        return self.host.call("ReadBatchExcelImport", {"sourcePath": str(target)})

    @staticmethod
    def data(row, width="1200", quantity="2"):
        return (f'<row r="{row}">' + cell("A", row, "物理行测试")
                + cell("B", row, quantity) + cell("C", row, width) + '</row>')

    def test_omitted_blank_rows_preserve_physical_numbers(self):
        response = self.workbook("sparse", [self.data(10), self.data(22)])
        self.assertTrue(response["ok"], response)
        rows = response["result"]["rows"]
        self.assertEqual([10, 22], [row["sourceRow"] for row in rows])
        self.assertEqual([1200, 1200], [row["parameters"]["width"] for row in rows])

    def test_self_closing_and_explicit_empty_rows_are_skipped(self):
        response = self.workbook("empty-rows", [
            '<row r="5"/>', self.data(10), '<row r="12"></row>', self.data(22)])
        self.assertTrue(response["ok"], response)
        self.assertEqual([10, 22], [row["sourceRow"] for row in response["result"]["rows"]])

    def test_last_excel_row_requires_only_one_data_record(self):
        response = self.workbook("last-row", [self.data(1048576)])
        self.assertTrue(response["ok"], response)
        self.assertEqual(1, len(response["result"]["rows"]))
        self.assertEqual(1048576, response["result"]["rows"][0]["sourceRow"])

    def test_numeric_error_identifies_the_physical_row(self):
        response = self.workbook("bad-number", [self.data(50, width="错误")])
        self.assertFalse(response["ok"], response)
        self.assertIn("第 50 行", response["error"])
        self.assertIn("必须为数字", response["error"])

    def test_quantity_error_identifies_the_physical_row(self):
        response = self.workbook("bad-quantity", [self.data(11, quantity="0")])
        self.assertFalse(response["ok"], response)
        self.assertIn("第 11 行", response["error"])
        self.assertIn("生产数量", response["error"])

    def test_required_error_identifies_the_physical_row(self):
        response = self.workbook("missing-width", [self.data(100, width="")])
        self.assertFalse(response["ok"], response)
        self.assertIn("第 100 行", response["error"])
        self.assertIn("不能为空", response["error"])

    def test_header_must_be_the_physical_second_row(self):
        response = self.workbook("moved-header", [self.data(10)], header_row=8)
        self.assertFalse(response["ok"], response)
        self.assertIn("缺少标题行和列标题", response["error"])

    def test_row_numbers_outside_the_excel_range_are_rejected(self):
        for row in (0, 1048577, 4294967296):
            with self.subTest(row=row):
                response = self.workbook("invalid-row-" + str(row), [self.data(row)])
                self.assertFalse(response["ok"], response)
                self.assertIn("工作表行号", response["error"])

    def test_duplicate_or_decreasing_row_numbers_are_rejected(self):
        for second in (10, 9):
            with self.subTest(second=second):
                response = self.workbook("unordered-row-" + str(second), [self.data(10), self.data(second)])
                self.assertFalse(response["ok"], response)
                self.assertIn("工作表行号", response["error"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    BatchExcelPhysicalRowsTest.bridge = arguments.bridge.resolve()
    BatchExcelPhysicalRowsTest.runtime = arguments.runtime.resolve()
    BatchExcelPhysicalRowsTest.output = arguments.output.resolve()
    unittest.main(argv=[__file__], verbosity=2)
