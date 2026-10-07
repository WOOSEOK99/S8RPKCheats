# -*- coding: utf-8 -*-
"""S8RPK Windows trait editor. Python 3.9+, standard library only.

Edits original 72 trait strings in the existing HEX-encoded trait_texts.ini
format. Edits additional traits directly in an external JSON file, preserving
all other JSON fields and formatting. Does not edit or patch game processes.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

APP_TITLE = "S8RPK 기재 이름·설명 편집기"
JSON_FILENAME = "S8RPK_traits_default.json"

# Default ROW catalog from TraitTextEditorData.cpp (main, 2026-10-07).
# ROW numbers are *editor row numbers*, not embedded/game trait IDs.
LEGACY_CATALOG = """\
1\t대덕\t교류 시 면회가 반드시 성공(혐오，원수，상극 등 제외)
2\t의협\t연계 대미지 상승
3\t만인적\t아군부대의 수보다 적부대의 수가 많으면 공격 %d 상승
4\t일신시담\t전의가 %d 이상이면 군략전법 무효화，이동제한(ZOC) 무효화
5\t금마초\t전의가 %d 이상이면 기병전법의 공격 %d 상승
6\t노당익장\t60세 이상이고 전의가 %d 이상이면 궁병전법의 공격 %d 상승
7\t복룡\t총대장/참군으로 참전해 책략을 사용하면，모든 아군부대의 전법 재사용 가능
8\t봉추\t총대장/참군으로 참전하면 아군의 초기 책략 게이지 %d%% 상승
9\t기린아\t%d일째 이후 1회 한정으로 자신부대의 전법 재사용 가능
10\t초세지걸\t전략P가 1 증가．평정에서 자신만 재행동 가능
11\t왕좌\t자신이 태수/군사인 도시에서 병사수입 증가
12\t불요불굴\t전의가 %d 이상이면 매일 주변 아군부대의 전의 %d 상승
13\t낭고\t총대장/참군으로 참전하면 적의 초기 전의 %d 저하
14\t병귀신속\t총대장/참군으로 참전하면 평지에서의 아군부대 이동 상승
15\t료래료래\t적부대를 격파하면 모든 적의 전의 %d 저하
16\t금강불괴\t전의가 %d 이상이면 방어 %d 상승
17\t위서심공\t이간의 효과량 상승．이간 성공 시 일정확률로 다른 무장도 충성 저하
18\t산도강습\t총대장/참군으로 참전하면 산에서의 아군부대 이동 상승
19\t강동맹호\t공격측 총대장으로 전투승리 시 병사 증가．무명이 높을수록 증가량 상승
20\t소패왕\t무력이 %d 이상인 부대 공격 시 보병기병궁병전법의 공격 %d 상승
21\t용재\t자신이 관여하는 연계효과가 높아짐
22\t화신\t전의가 %d 이상이면 불을 다룬 전법의 공격 %d 상승
23\t냉염\t공격할 적의 전의가 %d 이상이면 불을 다룬 전법의 공격 %d 상승
24\t괄목\t지력이 80 이상이면 담화 시 친밀이 추가로 상승．문명이 높을수록 효과량 상승
25\t방울감녕\t적부대를 격파하면 격파부대 주위에 있는 적의 전의 %d 저하
26\t원모심려\t자신이 태수/군사인 도시에서 전략P를 소비하지 않고 「이동」「배정」의 실행 가능
27\t천하무쌍\t매일 자신부대의 전의 %d 상승．보병기병궁병전법의 공격 %d 상승
28\t수화폐월\t친밀이 「미지」인 이성무장과 교류 시 「무욕」이 아닌 무장의 친밀이 「신뢰」가 됨
29\t명가위광\t자신이 징병 실행 시 치안이 저하하지 않음．무명/문명이 높을수록 징병수 증가
30\t악역무도\t자신이 태수인 도시에서 매년 금병사 증가，치안 저하．악명이 높을수록 효과량 상승
31\t구심\t지휘병사수가 %d 상승
32\t황천\t총대장의 기재가 「황천」이면 전투개시 시 전의가 %d 상승
33\t전장의꽃\t전투개시 시 전의가 %d 상승
34\t호위\t인접한 아군부대가 단기접전을 하게 될 시 대신 응전할 수 있으며，대행 시 무력이 %d 상승
35\t기습병\t삼림황무지 외로 이동해도 복병이 해제되지 않고 복병상태에서도 이동이 저하되지 않음
36\t간파\t함정이나 기습에 의한 피해를 입지 않음
37\t군규\t전의가 %d보다 적어지지 않음
38\t냉정\t전법 「매성」의 효과를 받지 않음
39\t기략\t군략계통 전법에 의한 피해가 경감
40\t진법\t병과계통 전법에 의한 피해가 경감
41\t궤계\t통상공격시 일정확률로 상대를 혼란상태로 만듦
42\t맹공\t「공격측」으로 출진시 공격이 %d 상승
43\t견수\t「수비측」으로 출진시 방어가 %d 상승
44\t불굴\t병사수가 출진시의 절반 이하이면 공격이 %d 상승
45\t산전\t지형 「산」에 있으면 병과전법의 공격 %d 상승
46\t삼전\t지형 「삼림」에 있으면 병과전법의 공격 %d 상승
47\t강창\t보병계병종으로 출진시 병과전법의 공격 %d 상승
48\t조기통제\t기병계병종으로 출진시 방어 %d 상승
49\t북방마술\t병과 「유목기병」으로 출진시 공격과 방어가 %d 상승
50\t질주궁\t궁병계병종으로 출진시 이동 상승
51\t수신\t함선의 이동 상승，함선계통 전법의 공격이 %d 상승
52\t상조교\t전법 「상병」의 공격이 %d 상승하며 1회 한정으로 재사용 가능
53\t재녀\t전법발동 시，해당 전법을 낮은 확률로 재사용 가능하게 함
54\t교화\t평정에 참가한 달，소속도시의 기술이 %d 상승
55\t호랑지심\t자신이 군주일 경우 동맹 파기를 실행해도 휘하무장의 충성이 떨어지지 않음
56\t부가\t매월 금을 %d 획득
57\t능리\t도시 내정의 소비행동력이 저하
58\t근면\t단련의 소비행동력이 저하
59\t시재\t연회의 소비행동력이 저하하고 분위기가 쉽게 좋아짐
60\t유심\t수양의 소비행동력이 저하
61\t경성\t이성무장과 담화할 시 상성 상관없이 분위기가 좋아짐
62\t직정\t함정이나 기습에 걸리기 쉬워지며 피해가 커짐
63\t자만\t적부대 격파시，전의가 %d 저하
64\t반감\t자신이 부대장으로 적부대 격파시，격파한 부대에 있는 적무장의 친밀이 상성에 따라 저하
65\t소심\t전의가 %d보다 적으면 공격과 방어가 %d 저하
66\t조급\t본거가 파괴되거나 상태이상에 빠지면 병사 감소
67\t성걸\t전법 「매성」을 받으면 일정확률로 혼란상태에 빠짐
68\t이기적\t연회의 분위기를 매우 나쁘게 만듦
69\t병약\t행동력의 최대치와 매월 회복량이 저하
70\t거만\t자신보다 명성이나 계급이 낮은 무장과의 담화 분위기가 좋아지기 힘듦
71\t괴물\t전의가 %d 이상이면 전의 저하 무효
72\t잔병첩보\t자신이 태수/군사/두령이면 매년 1월에 모든 도시가 %d개월 동안 첩보완료가 됨
"""

LEGACY_DEFAULTS = {}
for _s in LEGACY_CATALOG.splitlines():
    _index, _name, _desc = _s.split("\t", 2)
    LEGACY_DEFAULTS[int(_index)] = (_name, _desc)
RECORD_PATTERN = re.compile(rb"(ROW|ID):([0-9]+):([0-9A-Fa-f]*):([0-9A-Fa-f]*)")


def utf16_length(value: str) -> int:
    return len(value.encode("utf-16-le")) // 2


def format_tokens(value: str) -> list[str]:
    # Mirrors TraitTextEditorData.cpp's FormatTokens() algorithm.
    result = []
    pos = 0
    while pos < len(value):
        start = value.find("%", pos)
        if start < 0:
            break
        if start + 1 < len(value) and value[start + 1] == "%":
            pos = start + 2
            continue
        idx = start + 1
        while idx < len(value) and value[idx] in "-+ #0":
            idx += 1
        while idx < len(value) and value[idx].isdigit() and ord(value[idx]) < 128:
            idx += 1
        if idx < len(value) and value[idx] == ".":
            idx += 1
            while idx < len(value) and value[idx].isdigit() and ord(value[idx]) < 128:
                idx += 1
        if idx < len(value) and value[idx] in "diuoxXfFeEgGaAcsp":
            result.append(value[start:idx + 1])
            pos = idx + 1
        else:
            pos = start + 1
    return result


def validate_text(row: "TraitRecord") -> None:
    for value in (row.new_name, row.new_desc):
        if "\0" in value:
            raise ValueError(f"{row.key}: NUL(0) 문자를 사용할 수 없습니다.")
        try:
            value.encode("utf-8", "strict")
        except UnicodeEncodeError as err:
            raise ValueError(f"{row.key}: UTF-8로 저장할 수 없는 문자가 있습니다.") from err
    if utf16_length(row.new_name) > 5:
        raise ValueError(f"{row.key}: 변경 기재명은 UTF-16 기준 5자 이하여야 합니다.")
    if utf16_length(row.new_desc) > 512:
        raise ValueError(f"{row.key}: 변경 설명은 UTF-16 기준 512자 이하여야 합니다.")
    if row.new_desc and row.orig_desc and row.new_desc != row.orig_desc:
        a, b = format_tokens(row.orig_desc), format_tokens(row.new_desc)
        if b and a != b:
            raise ValueError(
                f"{row.key}: 설명의 %d 등 서식 종류·순서를 원본대로 유지하거나 "
                "서식 토큰을 모두 제거해야 합니다."
            )


@dataclass
class TraitRecord:
    key: int
    line_index: int
    new_name: str
    new_desc: str
    orig_name: str = ""
    orig_desc: str = ""
    loaded_name: str = ""
    loaded_desc: str = ""

    def edited(self) -> bool:
        return self.new_name != self.loaded_name or self.new_desc != self.loaded_desc

    def has_override(self) -> bool:
        return bool(self.new_name or self.new_desc)


class TraitFile:
    def __init__(self, path: Path):
        self.path = Path(path)
        if self.path.exists() and not self.path.is_file():
            raise ValueError("선택 경로가 파일이 아닙니다: " + str(self.path))
        self.exists = self.path.is_file()
        self.raw = self.path.read_bytes() if self.exists else b""
        self.digest = hashlib.sha256(self.raw).digest() if self.exists else None
        self.lines = self.raw.splitlines(keepends=True)
        self.mode = self.identify_type(self.raw, self.path.name)
        self.warnings: list[str] = []
        self.defaults = dict(LEGACY_DEFAULTS if self.mode == "ROW" else {})
        # Show every original trait even when there is no INI, or when an old
        # INI only contains a few saved rows. Missing rows are not overrides.
        self.records: dict[int, TraitRecord] = {
            key: TraitRecord(key, -1, "", "", name, desc)
            for key, (name, desc) in self.defaults.items()
        }
        self._read_records()

    @staticmethod
    def identify_type(raw: bytes, filename: str) -> str:
        for line in raw.splitlines():
            if line.startswith(b"[SAN8RPK_EMBEDDED_TRAIT_TEXTS]"):
                return "ID"
            if line.startswith(b"[SAN8RPK_TRAIT_TEXTS]"):
                return "ROW"
        typ = "ID" if filename.lower().endswith("_embedded.ini") else "ROW"
        if any(line.startswith(b"ID:") for line in raw.splitlines()):
            return "ID"
        if any(line.startswith(b"ROW:") for line in raw.splitlines()):
            return "ROW"
        return typ

    def _read_records(self):
        for line_index, whole in enumerate(self.lines):
            line = whole.rstrip(b"\r\n")
            if not (line.startswith(b"ROW:") or line.startswith(b"ID:")):
                continue
            match = RECORD_PATTERN.fullmatch(line)
            if not match or match.group(1).decode("ascii") != self.mode:
                self.warnings.append(f"{line_index + 1}행: 형식이 잘못되었거나 파일 종류가 다름")
                continue
            key = int(match.group(2))
            if not (1 <= key <= (72 if self.mode == "ROW" else 254)):
                self.warnings.append(f"{line_index + 1}행: 허용되지 않는 번호 {key}")
                continue
            try:
                name = bytes.fromhex(match.group(3).decode("ascii")).decode("utf-8", "strict")
                desc = bytes.fromhex(match.group(4).decode("ascii")).decode("utf-8", "strict")
            except (UnicodeDecodeError, ValueError):
                self.warnings.append(f"{line_index + 1}행: HEX/UTF-8 디코딩 실패")
                continue
            orig_name, orig_desc = self.defaults.get(key, ("", ""))
            if key not in self.records:
                self.warnings.append(f"{line_index + 1}행: 현재 기본 목록에 없는 번호 {key} (원본 유지)")
                continue
            if self.records[key].line_index >= 0:
                self.warnings.append(f"{line_index + 1}행: 중복 번호 {key} (마지막 값 사용)")
            self.records[key] = TraitRecord(
                key, line_index, name, desc, orig_name, orig_desc, name, desc
            )

    def modified_count(self) -> int:
        return sum(r.edited() for r in self.records.values())

    @staticmethod
    def _encode_line(mode: str, record: TraitRecord, ending: bytes) -> bytes:
        name_hex = record.new_name.encode("utf-8").hex().upper().encode("ascii")
        desc_hex = record.new_desc.encode("utf-8").hex().upper().encode("ascii")
        return (mode.encode("ascii") + b":" + str(record.key).encode("ascii")
                + b":" + name_hex + b":" + desc_hex + ending)

    def save(self) -> Path | None:
        updated = [r for r in self.records.values() if r.edited()]
        if not updated:
            return None
        for record in updated:
            validate_text(record)

        # Refuse to overwrite changes from another program, even if this file
        # did not exist when opened in the editor.
        if self.exists:
            if not self.path.is_file() or hashlib.sha256(self.path.read_bytes()).digest() != self.digest:
                raise RuntimeError("파일이 외부에서 변경되었습니다. 다시 열고 수정하세요.")
        elif self.path.exists():
            raise RuntimeError("작업 중 같은 이름의 INI가 생성되었습니다. 다시 열어 확인하세요.")

        lines = list(self.lines)
        added_positions = {}
        if not self.exists:
            header = (b"[SAN8RPK_TRAIT_TEXTS]\r\n" if self.mode == "ROW"
                      else b"[SAN8RPK_EMBEDDED_TRAIT_TEXTS]\r\n")
            lines = [header]
            # Match the game's serializer: include every known row, with an
            # empty HEX payload for untouched items.
            for key, record in sorted(self.records.items()):
                added_positions[key] = len(lines)
                lines.append(self._encode_line(self.mode, record, b"\r\n"))
        else:
            for record in updated:
                if record.line_index >= 0:
                    original = lines[record.line_index]
                    ending = (b"\r\n" if original.endswith(b"\r\n") else
                              b"\n" if original.endswith(b"\n") else
                              b"\r" if original.endswith(b"\r") else b"")
                    lines[record.line_index] = self._encode_line(self.mode, record, ending)
                else:
                    if lines and not lines[-1].endswith((b"\r", b"\n")):
                        lines[-1] += b"\r\n"
                    added_positions[record.key] = len(lines)
                    lines.append(self._encode_line(self.mode, record, b"\r\n"))

        new_data = b"".join(lines)
        backup = self.path.with_name(self.path.name + ".bak") if self.exists else None
        tmp_name = None
        try:
            if backup is not None:
                shutil.copy2(self.path, backup)
            with tempfile.NamedTemporaryFile("wb", dir=str(self.path.parent),
                                             prefix=".trait_text_", suffix=".tmp",
                                             delete=False) as temp:
                tmp_name = temp.name
                temp.write(new_data)
                temp.flush()
                os.fsync(temp.fileno())
            if self.exists:
                shutil.copymode(self.path, tmp_name)
            os.replace(tmp_name, self.path)
            tmp_name = None
        finally:
            if tmp_name and os.path.exists(tmp_name):
                os.unlink(tmp_name)

        self.exists = True
        self.raw = new_data
        self.lines = lines
        self.digest = hashlib.sha256(self.raw).digest()
        for key, position in added_positions.items():
            self.records[key].line_index = position
        for record in updated:
            record.loaded_name, record.loaded_desc = record.new_name, record.new_desc
        return backup




def _json_object_members(text: str, object_start: int):
    """Yield (key, value_start, value_end, value) from an existing JSON object.

    Positions refer to the original decoded text, allowing exact string-value
    substitutions without reserializing unrelated JSON or changing line endings.
    """
    decoder = json.JSONDecoder()

    def space(i):
        while i < len(text) and text[i] in " \t\r\n":
            i += 1
        return i

    i = space(object_start)
    if i >= len(text) or text[i] != "{":
        raise ValueError("JSON의 customNames 항목 형식이 올바르지 않습니다.")
    i += 1
    while True:
        i = space(i)
        if i >= len(text):
            raise ValueError("JSON 객체가 끝나지 않았습니다.")
        if text[i] == "}":
            return
        key, key_end = decoder.raw_decode(text, i)
        if not isinstance(key, str):
            raise ValueError("JSON 객체 키가 문자열이 아닙니다.")
        i = space(key_end)
        if i >= len(text) or text[i] != ":":
            raise ValueError("JSON 키 다음에 ':'가 없습니다.")
        value_start = space(i + 1)
        value, value_end = decoder.raw_decode(text, value_start)
        yield key, value_start, value_end, value
        i = space(value_end)
        if i < len(text) and text[i] == ",":
            i += 1
        elif i < len(text) and text[i] == "}":
            return
        else:
            raise ValueError("JSON 객체에서 구분 문자가 올바르지 않습니다.")


def validate_json_record(row: TraitRecord) -> None:
    """Validate newly edited JSON text; unchanged values may exceed UI limits."""
    if not row.edited():
        return
    if not row.new_name:
        raise ValueError(f"ID {row.key}: 기재명을 비울 수 없습니다.")
    for value in (row.new_name, row.new_desc):
        if "\0" in value:
            raise ValueError(f"ID {row.key}: NUL 문자를 사용할 수 없습니다.")
        try:
            value.encode("utf-8", "strict")
        except UnicodeEncodeError as err:
            raise ValueError(f"ID {row.key}: UTF-8 인코딩 오류입니다.") from err
    if row.new_name != row.loaded_name and utf16_length(row.new_name) > 5:
        raise ValueError(f"ID {row.key}: 변경 기재명은 UTF-16 기준 5자 이하여야 합니다.")
    if row.new_desc != row.loaded_desc and utf16_length(row.new_desc) > 512:
        raise ValueError(f"ID {row.key}: 변경 설명은 UTF-16 기준 512자 이하여야 합니다.")
    if row.new_desc != row.loaded_desc:
        original = format_tokens(row.loaded_desc)
        new = format_tokens(row.new_desc)
        if new and original != new:
            raise ValueError(f"ID {row.key}: 설명의 %d 등 서식 종류/순서를 유지하거나 모두 제거하세요.")


class JsonTraitFile:
    """Edit customNames[].name/desc in the *external* trait JSON in place."""

    def __init__(self, path: Path):
        self.path = Path(path)
        if not self.path.is_file():
            raise FileNotFoundError(
                f"추가 기재 설정 파일이 없습니다:\n{self.path}\n\n"
                "S8RPK_traits_default.json이 치트 DLL과 같은 폴더에 있는지 확인하세요."
            )
        self.mode = "JSON"
        self.exists = True
        self.raw = self.path.read_bytes()
        self.digest = hashlib.sha256(self.raw).digest()
        self.bom = self.raw.startswith(b"\xef\xbb\xbf")
        self.text = self.raw.decode("utf-8-sig", "strict")
        try:
            parsed = json.loads(self.text)
        except json.JSONDecodeError as err:
            raise ValueError(f"JSON 파일 형식 오류: {err}") from err
        if not isinstance(parsed, dict) or not isinstance(parsed.get("customNames"), dict):
            raise ValueError("S8RPK_traits_default.json에 customNames 객체가 없습니다.")
        self.warnings: list[str] = []
        self.records: dict[int, TraitRecord] = {}
        self.spans: dict[int, dict[str, tuple[int, int]]] = {}

        top = list(_json_object_members(self.text, 0))
        match = [(start, value) for key, start, _, value in top if key == "customNames"]
        if len(match) != 1:
            raise ValueError("JSON 최상위에 customNames 객체가 하나 있어야 합니다.")
        custom_start, custom = match[0]
        if not isinstance(custom, dict):
            raise ValueError("customNames 형식 오류")
        for key, start, _, value in _json_object_members(self.text, custom_start):
            if not key.isdigit() or not isinstance(value, dict):
                continue
            trait_id = int(key) + 1  # JSON key is zero-based custom index.
            if not 1 <= trait_id <= 254 or not isinstance(value.get("name"), str) or not value["name"]:
                continue
            if trait_id in self.records:
                raise ValueError(f"JSON에 중복 기재 ID가 있습니다: {trait_id}")
            if not isinstance(value.get("desc"), str):
                self.warnings.append(f"ID {trait_id}: desc 문자열이 없어 편집에서 제외함")
                continue
            fields = {}
            for field, fstart, fend, field_value in _json_object_members(self.text, start):
                if field in ("name", "desc") and isinstance(field_value, str):
                    if field in fields:
                        raise ValueError(f"ID {trait_id}: {field} 키가 중복되었습니다.")
                    fields[field] = (fstart, fend)
            if "name" not in fields or "desc" not in fields:
                continue
            self.spans[trait_id] = fields
            name, desc = value["name"], value["desc"]
            self.records[trait_id] = TraitRecord(
                trait_id, -1, name, desc, name, desc, name, desc
            )
        if not self.records:
            raise ValueError("customNames에서 편집할 추가 기재가 없습니다.")

    def modified_count(self) -> int:
        return sum(record.edited() for record in self.records.values())

    def save(self) -> Path | None:
        updated = [row for row in self.records.values() if row.edited()]
        if not updated:
            return None
        for row in updated:
            validate_json_record(row)
        if not self.path.is_file() or hashlib.sha256(self.path.read_bytes()).digest() != self.digest:
            raise RuntimeError("JSON이 다른 프로그램에서 변경되었습니다. 파일을 다시 열어 주세요.")

        replacements: list[tuple[int, int, str]] = []
        for row in updated:
            if row.new_name != row.loaded_name:
                a, b = self.spans[row.key]["name"]
                replacements.append((a, b, json.dumps(row.new_name, ensure_ascii=False)))
            if row.new_desc != row.loaded_desc:
                a, b = self.spans[row.key]["desc"]
                replacements.append((a, b, json.dumps(row.new_desc, ensure_ascii=False)))
        text = self.text
        for a, b, encoded in sorted(replacements, reverse=True):
            text = text[:a] + encoded + text[b:]
        # Don't write a damaged JSON if a future format differs from the catalog.
        json.loads(text)
        raw = (b"\xef\xbb\xbf" if self.bom else b"") + text.encode("utf-8")
        backup = self.path.with_name(self.path.name + ".bak")
        tmp_name = None
        try:
            shutil.copy2(self.path, backup)
            with tempfile.NamedTemporaryFile("wb", dir=str(self.path.parent),
                                             prefix=".trait_json_", suffix=".tmp",
                                             delete=False) as tmp:
                tmp_name = tmp.name
                tmp.write(raw)
                tmp.flush()
                os.fsync(tmp.fileno())
            shutil.copymode(self.path, tmp_name)
            os.replace(tmp_name, self.path)
            tmp_name = None
        finally:
            if tmp_name and os.path.exists(tmp_name):
                os.unlink(tmp_name)
        self.raw, self.text = raw, text
        self.digest = hashlib.sha256(raw).digest()
        # Refresh string value offsets for subsequent saves, without altering
        # the current editable UI record objects.
        loaded = JsonTraitFile(self.path)
        self.spans = loaded.spans
        for row in updated:
            row.orig_name = row.loaded_name = row.new_name
            row.orig_desc = row.loaded_desc = row.new_desc
        return backup


class TraitEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1120x745")
        self.minsize(850, 600)
        self.store: TraitFile | JsonTraitFile | None = None
        self.active_key: int | None = None
        self.path_var = tk.StringVar(value="기재 파일을 선택하세요.")
        self.query = tk.StringVar()
        self.orig_name = tk.StringVar(value="-")
        self.active_label = tk.StringVar(value="-")
        self.edit_name = tk.StringVar()
        self.info = tk.StringVar(value="기본 기재는 INI, 추가 기재는 외부 JSON을 직접 수정합니다.")
        self._build()
        self.query.trace_add("write", lambda *_: self._refresh_list())
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind_all("<Control-s>", lambda e: self._save())
        self.bind_all("<Control-o>", lambda e: self._open())
        self._try_open_startup()

    def _build(self):
        container = ttk.Frame(self, padding=12)
        container.pack(fill="both", expand=True)
        header = ttk.Frame(container)
        header.pack(fill="x")
        ttk.Button(header, text="기본 기재 (72개)", command=lambda: self._select_mode("ROW")).pack(side="left")
        ttk.Button(header, text="추가 기재 (JSON)", command=lambda: self._select_mode("JSON")).pack(side="left", padx=(6, 12))
        ttk.Button(header, text="파일 열기", command=self._open).pack(side="left")
        ttk.Button(header, text="저장 폴더 선택", command=self._choose_folder).pack(side="left", padx=(6, 0))
        ttk.Button(header, text="저장 (Ctrl+S)", command=self._save).pack(side="right")
        ttk.Button(header, text="메뉴 설명", command=self._show_menu_help).pack(side="right", padx=(0, 6))
        ttk.Label(container, textvariable=self.path_var, anchor="w").pack(fill="x", pady=(8, 8))
        main = ttk.PanedWindow(container, orient="horizontal")
        main.pack(fill="both", expand=True)

        left = ttk.Frame(main, padding=(0, 5, 6, 0))
        main.add(left, weight=2)
        search = ttk.Frame(left)
        search.pack(fill="x", pady=(0, 6))
        ttk.Label(search, text="검색").pack(side="left", padx=(0, 6))
        ttk.Entry(search, textvariable=self.query).pack(side="left", fill="x", expand=True)
        list_frame = ttk.Frame(left)
        list_frame.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(list_frame, columns=("no", "original", "changed"),
                                 show="headings", selectmode="browse")
        self.tree.heading("no", text="번호")
        self.tree.heading("original", text="기본 기재명")
        self.tree.heading("changed", text="변경 기재명")
        self.tree.column("no", width=58, anchor="center", stretch=False)
        self.tree.column("original", width=125, anchor="w")
        self.tree.column("changed", width=135, anchor="w")
        scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", self._select_row)
        self.left_hint = tk.StringVar()
        ttk.Label(left, textvariable=self.left_hint, foreground="#666666").pack(anchor="w", pady=(5, 0))

        right = ttk.Frame(main, padding=(8, 5, 0, 0))
        main.add(right, weight=3)
        ttk.Label(right, textvariable=self.active_label, font=("Malgun Gothic", 12, "bold")).pack(anchor="w")
        ttk.Label(right, text="기본 기재명", padding=(0, 10, 0, 2)).pack(anchor="w")
        ttk.Label(right, textvariable=self.orig_name).pack(anchor="w")
        self.name_hint = tk.StringVar()
        ttk.Label(right, textvariable=self.name_hint, padding=(0, 12, 0, 3)).pack(anchor="w")
        ttk.Entry(right, textvariable=self.edit_name, font=("Malgun Gothic", 11)).pack(fill="x")
        ttk.Label(right, text="기본 설명 (읽기 전용)", padding=(0, 15, 0, 3)).pack(anchor="w")
        self.original_desc = self._text_widget(right, 7)
        self.original_desc.configure(state="disabled")
        self.desc_hint = tk.StringVar()
        ttk.Label(right, textvariable=self.desc_hint, padding=(0, 13, 0, 3)).pack(anchor="w")
        self.edit_desc = self._text_widget(right, 8)
        row = ttk.Frame(right)
        row.pack(fill="x", pady=(11, 0))
        ttk.Button(row, text="이 항목 기본값 사용", command=self._reset_current).pack(side="left")
        ttk.Button(row, text="변경내용 반영", command=self._apply_current).pack(side="right")
        self.bottom_hint = tk.StringVar()
        ttk.Label(container, textvariable=self.bottom_hint,
                  foreground="#4a4a4a", wraplength=1080).pack(anchor="w", pady=(9, 3))
        ttk.Label(container, textvariable=self.info, anchor="w", wraplength=1080).pack(fill="x")

    def _show_menu_help(self):
        existing = getattr(self, "_help_window", None)
        if existing is not None and existing.winfo_exists():
            existing.lift()
            existing.focus_set()
            return

        dialog = tk.Toplevel(self)
        self._help_window = dialog
        dialog.title("메뉴 설명")
        dialog.geometry("690x530")
        dialog.minsize(520, 360)
        dialog.transient(self)

        frame = ttk.Frame(dialog, padding=14)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="S8RPK 기재 이름·설명 편집기 사용 안내",
                  font=("Malgun Gothic", 12, "bold")).pack(anchor="w", pady=(0, 10))

        content = (
            "[기본 사용 방법]\n"
            "• 이 프로그램(EXE)을 치트 DLL 및 S8RPK_traits_default.json과 같은 폴더에 둡니다.\n"
            "• 기본 기재는 trait_texts.ini가 있으면 자동으로 불러옵니다.\n"
            "• 기본 기재 INI가 없으면 내장된 원본 72개를 표시하고, 수정 후 저장할 때 새 INI를 만듭니다.\n"
            "• 추가 기재는 같은 폴더의 S8RPK_traits_default.json을 직접 불러와 수정합니다.\n"
            "• 두 파일 모두 저장 시 원본을 .bak으로 백업합니다(기본 INI 최초 생성 제외).\n\n"
            "[편집 대상]\n"
            "• 기본 기재 (72개): trait_texts.ini — 기존 HEX 형식 그대로 저장\n"
            "• 추가 기재 (JSON): S8RPK_traits_default.json — customNames의 이름·설명 직접 수정\n"
            "• 추가 기재의 목록과 설명은 현재 외부 JSON에서 읽으며 고정된 내장 목록을 사용하지 않습니다.\n\n"
            "[버튼 설명]\n"
            "• 기본 기재 / 추가 기재: 편집 대상을 전환합니다.\n"
            "• 파일 열기: 다른 폴더의 기본 INI 또는 외부 JSON을 수동으로 엽니다.\n"
            "• 저장 폴더 선택: 게임의 치트 DLL이 들어 있는 폴더를 지정합니다.\n"
            "• 검색: ID, 기본 이름, 수정 이름을 찾습니다.\n"
            "• 변경내용 반영: 입력값을 목록에 반영합니다(저장은 별도로 해야 함).\n"
            "• 이 항목 기본값 사용: 기본 INI는 수정값을 지우고, JSON은 열었을 때의 값으로 되돌립니다.\n"
            "• 저장 (Ctrl+S): 기본 기재는 INI, 추가 기재는 외부 JSON에 저장합니다.\n\n"
            "[주의사항]\n"
            "• JSON에서는 이름을 비울 수 없습니다. 기재 이름·설명만 수정하며 효과 수치는 그대로 둡니다.\n"
            "• 새 기재명은 UTF-16 기준 5자, 새 설명은 512자 이내로 제한합니다.\n"
            "• 설명의 %d 같은 서식은 종류·순서를 유지하거나 모두 제거하세요.\n"
            "• 게임을 종료한 상태에서 저장하고 다시 실행하여 적용을 확인하세요.\n"
            "• 기존 trait_texts_embedded.ini가 폴더에 남아 있으면 JSON의 이름·설명을 덮어쓸 수 있습니다.\n"
            "  사용하지 않는 기존 추가 기재 INI는 별도 백업 후 폴더 밖으로 옮기세요.\n"
            "• 외부 version.dll이 있는 구성은 별도 기재 설정을 사용하므로 적용되지 않을 수 있습니다."
        )
        body = ttk.Frame(frame)
        body.pack(fill="both", expand=True)
        scroll = ttk.Scrollbar(body, orient="vertical")
        help_text = tk.Text(body, wrap="word", font=("Malgun Gothic", 10),
                            padx=9, pady=8, yscrollcommand=scroll.set)
        scroll.configure(command=help_text.yview)
        help_text.insert("1.0", content)
        help_text.configure(state="disabled")
        help_text.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        ttk.Button(frame, text="닫기", command=dialog.destroy).pack(anchor="e", pady=(10, 0))
        dialog.focus_set()

    def _text_widget(self, parent: ttk.Frame, lines: int) -> tk.Text:
        frm = ttk.Frame(parent)
        frm.pack(fill="both", expand=True)
        widget = tk.Text(frm, height=lines, wrap="word", font=("Malgun Gothic", 10), undo=True)
        scroll = ttk.Scrollbar(frm, orient="vertical", command=widget.yview)
        widget.configure(yscrollcommand=scroll.set)
        widget.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        return widget

    def _try_open_startup(self):
        candidates = []
        if len(sys.argv) > 1:
            candidates.append(Path(sys.argv[1].strip('"')))
        locations = (Path(sys.executable).resolve().parent if getattr(sys, "frozen", False)
                     else Path(__file__).resolve().parent, Path.cwd())
        for folder in locations:
            candidates.append(folder / "trait_texts.ini")
        for path in candidates:
            if path.is_file():
                try:
                    self._load_file(path)
                    return
                except (OSError, ValueError):
                    pass
        self._load_file(locations[0] / "trait_texts.ini")

    def _select_mode(self, mode: str):
        if not self._allow_discard():
            return
        folder = self.store.path.parent if self.store else Path.cwd()
        filename = "trait_texts.ini" if mode == "ROW" else JSON_FILENAME
        try:
            self._load_file(folder / filename)
        except (OSError, ValueError) as exc:
            messagebox.showerror("파일 열기 실패", str(exc))

    def _choose_folder(self):
        if not self.store or not self._allow_discard():
            return
        folder = filedialog.askdirectory(
            title="S8RPK 치트 DLL이 있는 폴더 선택", initialdir=str(self.store.path.parent))
        if folder:
            filename = "trait_texts.ini" if self.store.mode == "ROW" else JSON_FILENAME
            try:
                self._load_file(Path(folder) / filename)
            except (OSError, ValueError) as exc:
                messagebox.showerror("폴더 선택 실패", str(exc))

    def _allow_discard(self) -> bool:
        if not self.store:
            return True
        self._stash_current()
        if self.store.modified_count() == 0:
            return True
        answer = messagebox.askyesnocancel("저장하지 않은 변경", "변경내용을 저장하시겠습니까?")
        if answer is None:
            return False
        if answer:
            return self._save()
        return True

    def _open(self):
        if not self._allow_discard():
            return
        initial_dir = self.store.path.parent if self.store else Path.cwd()
        filename = filedialog.askopenfilename(
            title="기본 기재 INI 또는 외부 추가 기재 JSON 선택",
            initialdir=str(initial_dir),
            filetypes=[("설정 파일", ("*.ini", "*.json")),
                       ("INI 파일", "*.ini"), ("JSON 파일", "*.json"), ("모든 파일", "*.*")]
        )
        if filename:
            try:
                self._load_file(Path(filename))
            except (OSError, ValueError) as exc:
                messagebox.showerror("열기 실패", str(exc))

    def _load_file(self, path: Path):
        if path.suffix.lower() == ".json":
            store = JsonTraitFile(path)
        elif path.suffix.lower() == ".ini":
            store = TraitFile(path)
            if store.mode != "ROW":
                raise ValueError("추가 기재는 INI가 아닌 S8RPK_traits_default.json을 직접 수정합니다.")
        else:
            raise ValueError("지원하는 파일은 기본 기재 INI와 외부 추가 기재 JSON입니다.")
        self.store = store
        self.active_key = None
        self.query.set("")
        json_mode = store.mode == "JSON"
        kind = "기본 기재 ROW" if not json_mode else "추가 기재 JSON"
        state = "기존 JSON 파일" if json_mode else (
            "기존 INI 파일" if store.exists else "INI 없음 · 수정 후 저장 시 생성")
        self.path_var.set(f"{store.path}  |  {kind} {len(store.records)}개  |  {state}")
        self.name_hint.set("수정할 기재명 (외부 JSON에 직접 저장)" if json_mode
                           else "수정할 기재명 (빈칸이면 기본 기재명 사용)")
        self.desc_hint.set("수정할 설명 (외부 JSON에 직접 저장)" if json_mode
                           else "수정할 설명 (빈칸이면 기본 설명 사용)")
        self.left_hint.set("※ JSON의 customNames 이름·설명만 수정하며 효과·등급은 그대로 둡니다."
                           if json_mode else "※ 기본명은 참고용이며 INI에는 변경값만 저장합니다.")
        self.bottom_hint.set(("JSON 이름을 비우지 마세요  |  수정 이름 최대 5자 / 설명 최대 512자(UTF-16 기준)  |  .bak 자동 백업"
                              if json_mode else
                              "빈 입력칸 = 치트 기본 문구  |  이름 최대 5자 / 설명 최대 512자(UTF-16 기준)  |  .bak 백업"))
        self._refresh_list(select_key=min(store.records))
        extra = f"  |  읽기 경고 {len(store.warnings)}개" if store.warnings else ""
        self.info.set(f"{state}.{extra} 게임을 종료한 뒤 저장하세요. 치트 DLL과 같은 폴더의 파일을 수정합니다.")
        if store.warnings:
            messagebox.showwarning("파일 읽기 경고", "\n".join(store.warnings[:10]) +
                                   ("\n…" if len(store.warnings) > 10 else ""))
        if json_mode and (store.path.parent / "trait_texts_embedded.ini").is_file():
            messagebox.showwarning(
                "기존 추가 기재 INI 감지",
                "이 폴더에 trait_texts_embedded.ini가 있습니다.\n"
                "치트가 이 파일의 저장된 이름·설명을 다시 적용하면 JSON 변경이 덮어써질 수 있습니다.\n\n"
                "기존 파일을 백업한 후, 더 이상 사용하지 않는다면 DLL 폴더 밖으로 옮겨 주세요."
            )

    def _stash_current(self):
        if not self.store or self.active_key is None:
            return
        record = self.store.records[self.active_key]
        record.new_name = self.edit_name.get()
        record.new_desc = self.edit_desc.get("1.0", "end-1c")

    def _refresh_list(self, select_key: int | None = None):
        if not hasattr(self, "tree"):
            return
        if self.store:
            self._stash_current()
        prev = select_key if select_key is not None else self.active_key
        self.tree.unbind("<<TreeviewSelect>>")
        self.tree.delete(*self.tree.get_children())
        if self.store:
            q = self.query.get().strip().casefold()
            for key, record in sorted(self.store.records.items()):
                target = f"{key} {record.orig_name} {record.new_name}".casefold()
                if q and q not in target:
                    continue
                changed = record.new_name or "(기본값)"
                if record.edited():
                    changed += " *"
                self.tree.insert("", "end", iid=str(key), values=(key, record.orig_name or "-", changed))
        self.tree.bind("<<TreeviewSelect>>", self._select_row)
        if prev is not None and self.tree.exists(str(prev)):
            self.tree.selection_set(str(prev))
            self.tree.see(str(prev))
            self._show_record(prev)
        elif self.tree.get_children():
            key = int(self.tree.get_children()[0])
            self.tree.selection_set(str(key))
            self._show_record(key)
        else:
            self.active_key = None
            self._clear_editor()

    def _clear_editor(self):
        self.active_label.set("선택된 항목 없음")
        self.orig_name.set("-")
        self.edit_name.set("")
        self.original_desc.configure(state="normal")
        self.original_desc.delete("1.0", "end")
        self.original_desc.configure(state="disabled")
        self.edit_desc.delete("1.0", "end")

    def _show_record(self, key: int):
        if not self.store:
            return
        self.active_key = key
        record = self.store.records[key]
        self.active_label.set(f"{self.store.mode} {key} | {record.orig_name or '(기본명 정보 없음)'}")
        self.orig_name.set(record.orig_name or "(현재 버전의 원본 이름 정보가 없습니다.)")
        self.edit_name.set(record.new_name)
        self.original_desc.configure(state="normal")
        self.original_desc.delete("1.0", "end")
        self.original_desc.insert("1.0", record.orig_desc or "(기본 설명 없음)")
        self.original_desc.configure(state="disabled")
        self.edit_desc.delete("1.0", "end")
        self.edit_desc.insert("1.0", record.new_desc)

    def _select_row(self, _event=None):
        selection = self.tree.selection()
        if not selection:
            return
        key = int(selection[0])
        if key == self.active_key:
            return
        self._stash_current()
        self._show_record(key)

    def _apply_current(self):
        if not self.store or self.active_key is None:
            return
        self._stash_current()
        try:
            (validate_json_record if self.store.mode == "JSON" else validate_text)(
                self.store.records[self.active_key])
        except ValueError as exc:
            messagebox.showerror("입력 확인", str(exc))
            return
        key = self.active_key
        self._refresh_list(select_key=key)
        self.info.set(f"변경된 항목 {self.store.modified_count()}개. 우측 상단 '파일 저장'을 눌러 저장하세요.")

    def _reset_current(self):
        if not self.store or self.active_key is None:
            return
        if self.store.mode == "JSON":
            row = self.store.records[self.active_key]
            self.edit_name.set(row.loaded_name)
            self.edit_desc.delete("1.0", "end")
            self.edit_desc.insert("1.0", row.loaded_desc)
        else:
            self.edit_name.set("")
            self.edit_desc.delete("1.0", "end")
        self._apply_current()

    def _save(self) -> bool:
        if not self.store:
            messagebox.showinfo("안내", "먼저 기재 파일을 여세요.")
            return False
        self._stash_current()
        changed = self.store.modified_count() > 0
        try:
            backup = self.store.save()
        except (ValueError, RuntimeError, OSError, UnicodeError) as exc:
            messagebox.showerror("저장 실패", str(exc))
            return False
        self._refresh_list(select_key=self.active_key)
        if not changed:
            self.info.set("저장할 변경사항이 없습니다.")
            return True
        if self.store.mode == "ROW":
            self.path_var.set(self.path_var.get().replace("INI 없음 · 수정 후 저장 시 생성", "기존 INI 파일"))
        note = f"백업: {backup.name}" if backup else "새 INI 파일 생성"
        self.info.set(f"저장 완료! {self.store.path}  ({note})")
        message = ("추가 기재를 외부 JSON에 직접 저장했습니다.\n"
                   if self.store.mode == "JSON" else
                   "기본 기재를 치트 호환 HEX INI 형식으로 저장했습니다.\n")
        messagebox.showinfo("저장 완료", f"{message}{note}\n\n게임을 다시 실행해 적용을 확인하세요.")
        return True

    def _on_close(self):
        if self._allow_discard():
            self.destroy()


if __name__ == "__main__":
    app = TraitEditor()
    app.mainloop()
