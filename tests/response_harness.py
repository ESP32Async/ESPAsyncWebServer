"""Compile the actual response methods with a controllable TCP peer."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def method(filename, signature):
    source = (ROOT / "src" / filename).read_text()
    start = source.index(signature + "(")
    # Production definitions use clang-format's unindented closing brace.
    end = source.index("\n}", start) + 2
    return source[start:end]


def run_cases(test, source, cases, definitions):
    with tempfile.TemporaryDirectory(prefix="async-response-test-") as directory:
        path = Path(directory)
        cpp = path / "response.cpp"
        cpp.write_text(source)
        binary = path / "response"
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-I", str(ROOT / "tests/native"),
            *definitions, str(cpp), "-o", str(binary),
        ], check=True)
        for case in cases:
            with test.subTest(case=case, definitions=definitions):
                result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
                test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
