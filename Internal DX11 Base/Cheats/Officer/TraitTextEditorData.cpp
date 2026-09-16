#include "pch.h"
#include "TraitTextEditorData.h"

#include "../../showlog.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace DX11Base {

extern HMODULE g_hModule;

namespace {

std::vector<TraitTextEditRow> g_rows = {
    {u8"대덕", u8"교류 시 면회가 반드시 성공(혐오，원수，상극 등 제외)", "", ""},
    {u8"의협", u8"연계 대미지 상승", "", ""},
    {u8"만인적", u8"아군부대의 수보다 적부대의 수가 많으면 공격 %d 상승", "", ""},
    {u8"일신시담", u8"전의가 %d 이상이면 군략전법 무효화，이동제한(ZOC) 무효화", "", ""},
    {u8"금마초", u8"전의가 %d 이상이면 기병전법의 공격 %d 상승", "", ""},
    {u8"노당익장", u8"60세 이상이고 전의가 %d 이상이면 궁병전법의 공격 %d 상승", "", ""},
    {u8"복룡", u8"총대장/참군으로 참전해 책략을 사용하면，모든 아군부대의 전법 재사용 가능", "", ""},
    {u8"봉추", u8"총대장/참군으로 참전하면 아군의 초기 책략 게이지 %d%% 상승", "", ""},
    {u8"기린아", u8"%d일째 이후 1회 한정으로 자신부대의 전법 재사용 가능", "", ""},
    {u8"초세지걸", u8"전략P가 1 증가．평정에서 자신만 재행동 가능", "", ""},
    {u8"왕좌", u8"자신이 태수/군사인 도시에서 병사수입 증가", "", ""},
    {u8"불요불굴", u8"전의가 %d 이상이면 매일 주변 아군부대의 전의 %d 상승", "", ""},
    {u8"낭고", u8"총대장/참군으로 참전하면 적의 초기 전의 %d 저하", "", ""},
    {u8"병귀신속", u8"총대장/참군으로 참전하면 평지에서의 아군부대 이동 상승", "", ""},
    {u8"료래료래", u8"적부대를 격파하면 모든 적의 전의 %d 저하", "", ""},
    {u8"금강불괴", u8"전의가 %d 이상이면 방어 %d 상승", "", ""},
    {u8"위서심공", u8"이간의 효과량 상승．이간 성공 시 일정확률로 다른 무장도 충성 저하", "", ""},
    {u8"산도강습", u8"총대장/참군으로 참전하면 산에서의 아군부대 이동 상승", "", ""},
    {u8"강동맹호", u8"공격측 총대장으로 전투승리 시 병사 증가．무명이 높을수록 증가량 상승", "", ""},
    {u8"소패왕", u8"무력이 %d 이상인 부대 공격 시 보병기병궁병전법의 공격 %d 상승", "", ""},
    {u8"용재", u8"자신이 관여하는 연계효과가 높아짐", "", ""},
    {u8"화신", u8"전의가 %d 이상이면 불을 다룬 전법의 공격 %d 상승", "", ""},
    {u8"냉염", u8"공격할 적의 전의가 %d 이상이면 불을 다룬 전법의 공격 %d 상승", "", ""},
    {u8"괄목", u8"지력이 80 이상이면 담화 시 친밀이 추가로 상승．문명이 높을수록 효과량 상승", "", ""},
    {u8"방울감녕", u8"적부대를 격파하면 격파부대 주위에 있는 적의 전의 %d 저하", "", ""},
    {u8"원모심려", u8"자신이 태수/군사인 도시에서 전략P를 소비하지 않고 「이동」「배정」의 실행 가능", "", ""},
    {u8"천하무쌍", u8"매일 자신부대의 전의 %d 상승．보병기병궁병전법의 공격 %d 상승", "", ""},
    {u8"수화폐월", u8"친밀이 「미지」인 이성무장과 교류 시 「무욕」이 아닌 무장의 친밀이 「신뢰」가 됨", "", ""},
    {u8"명가위광", u8"자신이 징병 실행 시 치안이 저하하지 않음．무명/문명이 높을수록 징병수 증가", "", ""},
    {u8"악역무도", u8"자신이 태수인 도시에서 매년 금병사 증가，치안 저하．악명이 높을수록 효과량 상승", "", ""},
    {u8"구심", u8"지휘병사수가 %d 상승", "", ""},
    {u8"황천", u8"총대장의 기재가 「황천」이면 전투개시 시 전의가 %d 상승", "", ""},
    {u8"전장의꽃", u8"전투개시 시 전의가 %d 상승", "", ""},
    {u8"호위", u8"인접한 아군부대가 단기접전을 하게 될 시 대신 응전할 수 있으며，대행 시 무력이 %d 상승", "", ""},
    {u8"기습병", u8"삼림황무지 외로 이동해도 복병이 해제되지 않고 복병상태에서도 이동이 저하되지 않음", "", ""},
    {u8"간파", u8"함정이나 기습에 의한 피해를 입지 않음", "", ""},
    {u8"군규", u8"전의가 %d보다 적어지지 않음", "", ""},
    {u8"냉정", u8"전법 「매성」의 효과를 받지 않음", "", ""},
    {u8"기략", u8"군략계통 전법에 의한 피해가 경감", "", ""},
    {u8"진법", u8"병과계통 전법에 의한 피해가 경감", "", ""},
    {u8"궤계", u8"통상공격시 일정확률로 상대를 혼란상태로 만듦", "", ""},
    {u8"맹공", u8"「공격측」으로 출진시 공격이 %d 상승", "", ""},
    {u8"견수", u8"「수비측」으로 출진시 방어가 %d 상승", "", ""},
    {u8"불굴", u8"병사수가 출진시의 절반 이하이면 공격이 %d 상승", "", ""},
    {u8"산전", u8"지형 「산」에 있으면 병과전법의 공격 %d 상승", "", ""},
    {u8"삼전", u8"지형 「삼림」에 있으면 병과전법의 공격 %d 상승", "", ""},
    {u8"강창", u8"보병계병종으로 출진시 병과전법의 공격 %d 상승", "", ""},
    {u8"조기통제", u8"기병계병종으로 출진시 방어 %d 상승", "", ""},
    {u8"북방마술", u8"병과 「유목기병」으로 출진시 공격과 방어가 %d 상승", "", ""},
    {u8"질주궁", u8"궁병계병종으로 출진시 이동 상승", "", ""},
    {u8"수신", u8"함선의 이동 상승，함선계통 전법의 공격이 %d 상승", "", ""},
    {u8"상조교", u8"전법 「상병」의 공격이 %d 상승하며 1회 한정으로 재사용 가능", "", ""},
    {u8"재녀", u8"전법발동 시，해당 전법을 낮은 확률로 재사용 가능하게 함", "", ""},
    {u8"교화", u8"평정에 참가한 달，소속도시의 기술이 %d 상승", "", ""},
    {u8"호랑지심", u8"자신이 군주일 경우 동맹 파기를 실행해도 휘하무장의 충성이 떨어지지 않음", "", ""},
    {u8"부가", u8"매월 금을 %d 획득", "", ""},
    {u8"능리", u8"도시 내정의 소비행동력이 저하", "", ""},
    {u8"근면", u8"단련의 소비행동력이 저하", "", ""},
    {u8"시재", u8"연회의 소비행동력이 저하하고 분위기가 쉽게 좋아짐", "", ""},
    {u8"유심", u8"수양의 소비행동력이 저하", "", ""},
    {u8"경성", u8"이성무장과 담화할 시 상성 상관없이 분위기가 좋아짐", "", ""},
    {u8"직정", u8"함정이나 기습에 걸리기 쉬워지며 피해가 커짐", "", ""},
    {u8"자만", u8"적부대 격파시，전의가 %d 저하", "", ""},
    {u8"반감", u8"자신이 부대장으로 적부대 격파시，격파한 부대에 있는 적무장의 친밀이 상성에 따라 저하", "", ""},
    {u8"소심", u8"전의가 %d보다 적으면 공격과 방어가 %d 저하", "", ""},
    {u8"조급", u8"본거가 파괴되거나 상태이상에 빠지면 병사 감소", "", ""},
    {u8"성걸", u8"전법 「매성」을 받으면 일정확률로 혼란상태에 빠짐", "", ""},
    {u8"이기적", u8"연회의 분위기를 매우 나쁘게 만듦", "", ""},
    {u8"병약", u8"행동력의 최대치와 매월 회복량이 저하", "", ""},
    {u8"거만", u8"자신보다 명성이나 계급이 낮은 무장과의 담화 분위기가 좋아지기 힘듦", "", ""},
    {u8"괴물", u8"전의가 %d 이상이면 전의 저하 무효", "", ""},
    {u8"잔병첩보", u8"자신이 태수/군사/두령이면 매년 1월에 모든 도시가 %d개월 동안 첩보완료가 됨", "", ""},
};

void SetError(std::string* error, const std::string& text) {
  if (error)
    *error = text;
}

bool Utf8ToWide(const std::string& text, std::wstring& out) {
  if (text.find('\0') != std::string::npos)
    return false;
  if (text.empty()) {
    out.clear();
    return true;
  }

  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0)
    return false;

  out.resize(static_cast<std::size_t>(count));
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), out.data(), count) == count;
}

std::vector<std::string> FormatTokens(const std::string& text) {
  std::vector<std::string> out;
  std::size_t pos = 0;

  while (pos < text.size()) {
    const std::size_t p = text.find('%', pos);
    if (p == std::string::npos)
      break;

    if (p + 1 < text.size() && text[p + 1] == '%') {
      pos = p + 2;
      continue;
    }

    std::size_t i = p + 1;
    while (i < text.size() && std::string("-+ #0").find(text[i]) != std::string::npos)
      ++i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
      ++i;
    if (i < text.size() && text[i] == '.') {
      ++i;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
        ++i;
    }

    if (i < text.size() && std::string("diuoxXfFeEgGaAcsp").find(text[i]) != std::string::npos) {
      out.emplace_back(text.substr(p, i - p + 1));
      pos = i + 1;
    } else {
      // CT와 동일하게 알 수 없는 '%'는 일반 표시 문자로 취급합니다.
      pos = p + 1;
    }
  }

  return out;
}

std::string HexEncode(const std::string& text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 2);
  for (unsigned char ch : text) {
    out.push_back(kHex[ch >> 4]);
    out.push_back(kHex[ch & 0x0F]);
  }
  return out;
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

bool HexDecode(const std::string& text, std::string& out) {
  if ((text.size() & 1u) != 0)
    return false;

  out.clear();
  out.reserve(text.size() / 2);
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const int hi = HexValue(text[i]);
    const int lo = HexValue(text[i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out.push_back(static_cast<char>((hi << 4) | lo));
  }
  return true;
}

bool ParseRowLine(const std::string& line, std::size_t& index, std::string& name, std::string& desc) {
  if (line.rfind("ROW:", 0) != 0)
    return false;

  const std::size_t p1 = line.find(':', 4);
  if (p1 == std::string::npos)
    return false;
  const std::size_t p2 = line.find(':', p1 + 1);
  if (p2 == std::string::npos)
    return false;

  try {
    const unsigned long parsed = std::stoul(line.substr(4, p1 - 4));
    if (parsed == 0 || parsed > g_rows.size())
      return false;
    index = static_cast<std::size_t>(parsed - 1);
  } catch (...) {
    return false;
  }

  return HexDecode(line.substr(p1 + 1, p2 - p1 - 1), name) &&
         HexDecode(line.substr(p2 + 1), desc);
}

} // namespace

std::vector<TraitTextEditRow>& GetTraitTextEditRows() {
  return g_rows;
}

std::string GetTraitTextStoragePath() {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(g_hModule, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
    return "trait_texts.ini";

  return (std::filesystem::path(path).parent_path() / L"trait_texts.ini").string();
}

bool LoadTraitTextEdits(std::string* error) {
  ResetTraitTextEdits();

  const std::string path = GetTraitTextStoragePath();
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    SetError(error, "trait_texts.ini가 없습니다.");
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    std::size_t index = 0;
    std::string name;
    std::string desc;
    if (!ParseRowLine(line, index, name, desc))
      continue;

    g_rows[index].newName = std::move(name);
    g_rows[index].newDesc = std::move(desc);
  }

  return true;
}

bool SaveTraitTextEdits(std::string* error) {
  for (std::size_t i = 0; i < g_rows.size(); ++i) {
    if (!ValidateTraitTextRow(i, error))
      return false;
  }

  const std::string path = GetTraitTextStoragePath();
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    SetError(error, "trait_texts.ini 저장 파일을 열 수 없습니다: " + path);
    return false;
  }

  file << "[SAN8RPK_TRAIT_TEXTS]\r\n";
  for (std::size_t i = 0; i < g_rows.size(); ++i) {
    file << "ROW:" << (i + 1) << ':'
         << HexEncode(g_rows[i].newName) << ':'
         << HexEncode(g_rows[i].newDesc) << "\r\n";
  }

  if (!file.good()) {
    SetError(error, "trait_texts.ini 저장 중 쓰기 오류가 발생했습니다.");
    return false;
  }

  return true;
}

void ResetTraitTextEdits() {
  for (auto& row : g_rows) {
    row.newName.clear();
    row.newDesc.clear();
  }
}

bool HasTraitTextEdits() {
  for (const auto& row : g_rows) {
    if ((!row.newName.empty() && row.newName != row.oldName) ||
        (!row.newDesc.empty() && row.newDesc != row.oldDesc))
      return true;
  }
  return false;
}

bool ValidateTraitTextRow(std::size_t index, std::string* error) {
  if (index >= g_rows.size()) {
    SetError(error, "기재 인덱스가 범위를 벗어났습니다.");
    return false;
  }

  const auto& row = g_rows[index];
  std::wstring wide;

  if (!Utf8ToWide(row.newName, wide)) {
    SetError(error, row.oldName + ": 새 기재명이 올바른 UTF-8 문자열이 아닙니다.");
    return false;
  }
  if (wide.size() > 5) {
    SetError(error, row.oldName + ": 새 기재명은 최대 5글자입니다.");
    return false;
  }

  if (!Utf8ToWide(row.newDesc, wide)) {
    SetError(error, row.oldName + ": 새 설명이 올바른 UTF-8 문자열이 아닙니다.");
    return false;
  }
  if (wide.size() > 512) {
    SetError(error, row.oldName + ": 문구는 UTF-16 기준 512자 이내로 입력하세요.");
    return false;
  }

  if (!row.newDesc.empty() && row.newDesc != row.oldDesc) {
    const auto originalTokens = FormatTokens(row.oldDesc);
    const auto newTokens = FormatTokens(row.newDesc);
    if (!newTokens.empty() && newTokens != originalTokens) {
      SetError(error, row.oldName +
               ": %d 등의 순서와 종류를 원문과 같게 유지하세요. 수치를 모두 생략하는 것은 가능합니다.");
      return false;
    }
  }

  return true;
}

} // namespace DX11Base
