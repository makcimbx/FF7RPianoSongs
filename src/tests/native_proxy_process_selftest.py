"""Build/run ONLY the owned synthetic host/DLLs; never launches or loads a game.

Build order: stub import lib -> final host -> exact host profile -> real static/late
DLL copies + forward target -> owned child. No product/CMake source is modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def profile(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[:2] == b"MZ" and data[pe:pe + 4] == b"PE\0\0"
    machine, sections, timestamp = struct.unpack_from("<HHI", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    assert machine == 0x8664 and struct.unpack_from("<H", data, opt)[0] == 0x20b
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    size, checksum = struct.unpack_from("<I", data, opt + 56)[0], struct.unpack_from("<I", data, opt + 64)[0]
    for i in range(sections):
        section = opt + optional_size + 40 * i
        _, va, raw_size, raw = struct.unpack_from("<IIII", data, section + 8)
        if va <= entry and entry + 32 <= va + raw_size:
            signature = data[raw + entry - va:raw + entry - va + 32]
            assert len(signature) == 32 and timestamp and checksum
            return timestamp, size, checksum, entry, signature
    raise AssertionError("host AEP lacks a complete file-backed signature")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vcvars", type=Path, default=Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"))
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    parent = Path(os.environ["LOCALAPPDATA"]) / "Temp" / "opencode"
    build = args.build_dir.resolve() if args.build_dir else Path(tempfile.mkdtemp(prefix="native-proxy-process-", dir=parent))
    if not build.is_relative_to(parent.resolve()) or build == parent.resolve():
        parser.error("--build-dir must be a dedicated child of LOCALAPPDATA/Temp/opencode")
    build.mkdir(parents=True, exist_ok=True)
    tests = root / "src" / "tests"
    inputs = [root / "src/proxy" / name for name in ("entry_bridge.h", "entry_bridge.cpp", "entry_thunk.asm")]
    inputs += [tests / name for name in ("native_proxy_process_protocol.h", "native_proxy_process_host.cpp",
                                        "native_proxy_process_dll.cpp", "native_proxy_process_target.cpp")]
    source_hashes = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    flags = f'/nologo /c /O2 /Oi /GS- /GR- /Zl /std:c++20 /W4 /WX /I "{root / "src"}" /I "{build}"'
    link = "/nologo /nodefaultlib /incremental:no /release /dynamicbase /nxcompat /machine:x64"
    log = build / "build.log"

    def run_batch(name, commands):
        batch = build / name
        batch.write_text(f'@echo off\ncall "{args.vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
                         + "\n".join(command + "\nif errorlevel 1 exit /b 1" for command in commands) + "\n", encoding="utf-8")
        result = subprocess.run(["cmd", "/d", "/c", str(batch)], cwd=build, capture_output=True, text=True, timeout=120)
        with log.open("a", encoding="utf-8") as out:
            out.write(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {log}\n{result.stdout}{result.stderr}")

    (build / "stub.def").write_text("LIBRARY native_proxy_process_fixture\nEXPORTS\n fixture_probe\n fixture_add\n")
    run_batch("build-host.cmd", [
        "lib /nologo /machine:x64 /def:stub.def /out:fixture_stub.lib",
        f'cl {flags} /Fo:host.obj "{tests / "native_proxy_process_host.cpp"}"',
        f"link {link} /subsystem:console /entry:fixture_host_entry /out:native_proxy_process_host.exe host.obj fixture_stub.lib kernel32.lib",
    ])
    host = build / "native_proxy_process_host.exe"
    fingerprint = hashlib.sha256(host.read_bytes()).hexdigest()
    timestamp, size, checksum, entry, signature = profile(host)
    values = ",".join(f"0x{b:02x}" for b in signature)
    (build / "native_proxy_process_profile.generated.h").write_text(
        f"#pragma once\ninline constexpr ff7rp::proxy::EntryProfile kHostProfile{{0x{timestamp:x},0x{size:x},0x{checksum:x},0x{entry:x},32,{{{values}}}}};\n")
    (build / "fixture.def").write_text("LIBRARY native_proxy_process_fixture\nEXPORTS\n fixture_probe\n fixture_add=native_proxy_process_target.fixture_target_add\n")
    run_batch("build-dlls.cmd", [
        f'cl {flags} /Fo:target.obj "{tests / "native_proxy_process_target.cpp"}"',
        f"link {link} /dll /noentry /export:fixture_target_add /out:native_proxy_process_target.dll target.obj",
        f'cl {flags} /Fo:bridge.obj "{root / "src/proxy/entry_bridge.cpp"}"',
        f'ml64 /nologo /c /Fo thunk.obj "{root / "src/proxy/entry_thunk.asm"}"',
        f'cl {flags} /Fo:fixture.obj "{tests / "native_proxy_process_dll.cpp"}"',
        f"link {link} /dll /entry:DllMain /def:fixture.def /implib:fixture_actual.lib /out:native_proxy_process_fixture.dll fixture.obj bridge.obj thunk.obj native_proxy_process_target.lib kernel32.lib",
        f"link {link} /dll /entry:DllMain /export:fixture_probe /implib:late.lib /out:native_proxy_process_late.dll fixture.obj bridge.obj thunk.obj kernel32.lib",
    ])
    assert hashlib.sha256(host.read_bytes()).hexdigest() == fingerprint, "host changed after profile capture"
    assert all(hashlib.sha256((root / path).read_bytes()).hexdigest() == value for path, value in source_hashes.items()), "fixture source changed during build"
    result = subprocess.run([str(host)], cwd=build, capture_output=True, text=True, timeout=20)
    (build / "child-stdout.log").write_text(result.stdout)
    (build / "child-stderr.log").write_text(result.stderr)
    evidence = {"kind": "owned_synthetic_static_import", "host_sha256": fingerprint, "source_sha256": source_hashes,
                "dll_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in build.glob("native_proxy_process_*.dll")},
                "profile": {"timestamp": timestamp, "size": size, "checksum": checksum, "entry_rva": entry, "signature": signature.hex()},
                "exit_code": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
    (build / "result.json").write_text(json.dumps(evidence, indent=2))
    if result.returncode:
        raise RuntimeError(f"owned child failed ({result.returncode}); see {build / 'result.json'}")
    observed = json.loads(result.stdout)
    assert observed["callback_count"] == 1 and observed["forwarded_result"] == 65 and all(v for v in observed.values())
    print(f"native proxy real static-import process selftest passed: {build}")


if __name__ == "__main__":
    main()
