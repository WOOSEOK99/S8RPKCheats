"""Compile unchanged production helpers to an assembly listing; do not run them."""
from pathlib import Path
import subprocess

OUT = Path(__file__).resolve().parent
vs = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC")
sdk = Path(r"C:\Program Files (x86)\Windows Kits\10\Include")
vc = sorted(vs.iterdir())[-1]
kit = sorted(sdk.iterdir())[-1]
args = [str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/c", "/O2", "/std:c++20",
        "/EHsc", "/utf-8", "/Zc:char8_t-", "/FAsc",
        "/Fa" + str(OUT / "abi_probe.asm"), "/Fo" + str(OUT / "abi_probe.obj"),
        "/I" + str(vc / "include")]
args += ["/I" + str(kit / p) for p in ("ucrt", "shared", "um", "winrt")]
args += [str(OUT / "abi_probe.cpp")]
print("Compiler:", vc.name, "SDK:", kit.name, flush=True)
result = subprocess.run(args, cwd=OUT, capture_output=True)
(OUT / "abi_compile.log").write_bytes(result.stdout + result.stderr)
print((result.stdout + result.stderr).decode("utf-8", errors="replace"))
if result.returncode:
    raise SystemExit(result.returncode)

# Compile and link a separate, synthetic-object-only executable.
asm_obj = OUT / "capture_return.obj"
subprocess.run([str(vc / "bin/Hostx64/x64/ml64.exe"), "/nologo", "/c",
                "/Fo" + str(asm_obj), str(OUT / "capture_return.asm")], check=True, cwd=OUT)
repro_args = [a for a in args if not a.startswith(("/Fa", "/Fo")) and a not in ("/FAsc", str(OUT / "abi_probe.cpp"))]
repro_args += ["/Fo" + str(OUT / "abi_repro.obj"), str(OUT / "abi_repro.cpp")]
subprocess.run(repro_args, check=True, cwd=OUT)
sdk_lib = kit.parent.parent / "Lib" / kit.name
link_args = [str(vc / "bin/Hostx64/x64/link.exe"), "/nologo", "/MACHINE:X64",
             "/OUT:" + str(OUT / "abi_repro.exe"), str(OUT / "abi_repro.obj"), str(asm_obj),
             "/LIBPATH:" + str(vc / "lib/x64"), "/LIBPATH:" + str(sdk_lib / "ucrt/x64"),
             "/LIBPATH:" + str(sdk_lib / "um/x64"), "kernel32.lib"]
subprocess.run(link_args, check=True, cwd=OUT)
run = subprocess.run([str(OUT / "abi_repro.exe")], capture_output=True, check=True, cwd=OUT)
(OUT / "abi_repro_result.txt").write_bytes(run.stdout + run.stderr)
print(run.stdout.decode("utf-8", errors="replace"))
