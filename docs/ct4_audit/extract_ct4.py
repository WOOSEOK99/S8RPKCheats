"""Offline CT 92011 evidence extraction. Never opens or modifies a game process."""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
CT = ROOT / "x64/Release/SAN8RPK-command-code-1.CT"

def main():
    sys.path.insert(0, str(Path(tempfile.gettempdir()) / "ct4-audit-deps"))
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    raw = CT.read_bytes()
    entry = next(e for e in ET.fromstring(raw).iter("CheatEntry")
                 if e.findtext("ID") == "92011")
    script = entry.findtext("AssemblerScript")
    (OUT / "ct4_original.lua").write_text(script, encoding="utf-8")
    hexes = lambda s: "".join(re.findall(r'"([0-9a-f]+)"', s))
    blob = bytes.fromhex(hexes(script.split('["blob"]=', 1)[1].split('["relocs"]', 1)[0]))
    relocs_text = script.split('["relocs"]=', 1)[1].split('["guards"]', 1)[0]
    relocs = [(int(a), int(b)) for a, b in re.findall(r'\{(\d+),\s*"base",\s*(\d+)\}', relocs_text)]
    guard_text = script.split('["guards"]=', 1)[1].split('["hooks"]', 1)[0]
    guards = [(int(m.group(1)), bytes.fromhex(hexes(m.group(2))))
              for m in re.finditer(r'\{(\d+),\s*(.*?)\}', guard_text, re.S)]
    hook_text = script.split('["hooks"]=', 1)[1].split('["features"]', 1)[0]
    hooks = [(int(a), bytes.fromhex(b), int(c)) for a, b, c in re.findall(
        r'\["rva"\]=(\d+),\s*\["original"\]="([0-9a-f]+)",\s*\["offset"\]=(\d+)', hook_text)]
    assert len(hooks) == 10 and len(guards) == 18 and len(relocs) == 23
    for rva, code, offset in hooks:
        enclosing = [(g, b) for g, b in guards if g <= rva and rva + len(code) <= g + len(b)]
        assert enclosing and all(b[rva-g:rva-g+len(code)] == code for g, b in enclosing)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    def disasm(data, address):
        return "\n".join(f"{i.address:08X}  {i.bytes.hex(' '):<44} {i.mnemonic} {i.op_str}"
                         for i in md.disasm(data, address))
    sections = []
    # Code and relocation tables are intentionally separated; do not decode padding/data.
    for name, start, end in [("hooks 1-6", 0, 0x590), ("helpers", 0x628, 0x901),
                              ("wrapper 7/8", 0x2000, 0x204e),
                              ("wrapper 9/10", 0x2200, 0x225a)]:
        sections.append(f"\n=== {name} [{start:X}, {end:X}) ===\n" + disasm(blob[start:end], start))
    sections.append("\n=== relocations: blob offset -> EXE RVA ===\n" +
                    "\n".join(f"{a:04X} -> EXE+{b:X}" for a, b in relocs))
    for n, (rva, code) in enumerate(guards, 1):
        # This guard begins inside an instruction; decode at its first provable
        # full instruction, retaining all original bytes in the XML/Lua evidence.
        if rva == 0x144A30A:
            code, rva = code[4:], rva + 4
        sections.append(f"\n=== guard {n} EXE+{rva:X}, {len(code)} bytes ===\n" + disasm(code, rva))
    for n, (rva, code, offset) in enumerate(hooks, 1):
        sections.append(f"\n=== hook {n} EXE+{rva:X}, {len(code)} bytes, blob+{offset:X} ===\n" + disasm(code, rva))
    (OUT / "ct4_disassembly.txt").write_text("\n".join(sections), encoding="utf-8")
    (OUT / "ct4_blob.bin").write_bytes(blob)
    dll_path = ROOT / "x64/Release/version.dll"
    if dll_path.exists():
        dll = dll_path.read_bytes()
        pe = struct.unpack_from("<I", dll, 0x3c)[0]
        count, = struct.unpack_from("<H", dll, pe + 6)
        optsize, = struct.unpack_from("<H", dll, pe + 20)
        sec = pe + 24 + optsize
        def dll_bytes(rva, size):
            for i in range(count):
                virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", dll, sec + 40*i + 8)
                if virtual_address <= rva < virtual_address + raw_size:
                    offset = raw_address + rva - virtual_address
                    return dll[offset:offset+size]
            raise ValueError(f"Unmapped DLL RVA {rva:X}")
        signatures_text = script.split('["versionSignatures"]=', 1)[1].split('-- CE 7.2', 1)[0]
        signatures = [(int(m.group(1)), bytes.fromhex(hexes(m.group(2))))
                      for m in re.finditer(r'\{(\d+),\s*(.*?)\}', signatures_text, re.S)]
        signature_results = [f"+{rva:X} {len(code)} bytes: " + ("PASS" if dll_bytes(rva, len(code)) == code else "FAIL")
                             for rva, code in signatures]
        (OUT / "version_wrapper_disassembly.txt").write_text(
            "MD5 " + hashlib.md5(dll).hexdigest() + "\n" + "\n".join(signature_results) + "\n" +
            disasm(dll_bytes(0xB5C70, 0x370), 0xB5C70), encoding="utf-8")
    manifest = {"source": str(CT.relative_to(ROOT)), "sha256": hashlib.sha256(raw).hexdigest(),
                "id": 92011, "blob_bytes": len(blob), "relocs": len(relocs), "guards": len(guards),
                "hooks": [{"id": n, "rva": hex(rva), "size": len(code), "blob_offset": hex(offset)}
                          for n, (rva, code, offset) in enumerate(hooks, 1)]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
