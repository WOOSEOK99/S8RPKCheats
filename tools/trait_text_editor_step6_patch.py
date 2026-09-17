from pathlib import Path

# Header: expose auto-apply tick.
p = Path('Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.h')
s = p.read_text(encoding='utf-8-sig')
old = 'void OpenTraitTextEditorWindow();\nvoid DrawTraitTextEditorWindow(float scale);\nbool IsTraitTextEditorWindowOpen();\n'
new = 'void OpenTraitTextEditorWindow();\nvoid DrawTraitTextEditorWindow(float scale);\nbool IsTraitTextEditorWindowOpen();\n// Step 6: 저장된 trait_texts.ini를 게임 준비 후 한 번 자동 적용합니다.\nvoid TickTraitTextEditorAutoApply();\n'
if old not in s:
    raise SystemExit('header anchor missing')
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8-sig')

# CPP: add bounded delayed auto-apply state and tick.
p = Path('Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp')
s = p.read_text(encoding='utf-8-sig')
anchor = 'static std::string g_status;\n'
insert = '''static std::string g_status;\n\n// Step 6 자동 적용 상태. 메뉴 렌더링이 시작된 뒤 잠시 기다렸다가 저장값을 적용합니다.\nstatic bool g_autoLoaded = false;\nstatic bool g_autoFinished = false;\nstatic ULONGLONG g_autoFirstTick = 0;\nstatic ULONGLONG g_autoLastAttempt = 0;\nstatic int g_autoAttempts = 0;\n'''
if anchor not in s:
    raise SystemExit('cpp state anchor missing')
s = s.replace(anchor, insert, 1)

# Manual apply success prevents later auto re-apply.
old = '''    if (ApplyAll(error))\n      SetStatus(u8"적용 완료");\n    else\n      SetStatus(std::string(u8"적용 실패: ") + error);\n'''
new = '''    if (ApplyAll(error)) {\n      g_autoFinished = true;\n      SetStatus(u8"적용 완료");\n    } else {\n      SetStatus(std::string(u8"적용 실패: ") + error);\n    }\n'''
if old not in s:
    raise SystemExit('manual apply block missing')
s = s.replace(old, new, 1)

# Manual remove must not be undone by auto-apply.
old = '''    if (RemoveAll(error))\n      SetStatus(u8"적용 해제 완료");\n    else\n      SetStatus(std::string(u8"적용 해제 실패: ") + error);\n'''
new = '''    g_autoFinished = true;\n    if (RemoveAll(error))\n      SetStatus(u8"적용 해제 완료");\n    else\n      SetStatus(std::string(u8"적용 해제 실패: ") + error);\n'''
if old not in s:
    raise SystemExit('manual remove block missing')
s = s.replace(old, new, 1)

# Insert tick before Draw function.
anchor = 'void DrawTraitTextEditorWindow(float scale) {\n'
tick = '''void TickTraitTextEditorAutoApply() {\n  if (g_autoFinished)\n    return;\n\n  if (!g_autoLoaded) {\n    std::string error;\n    if (!LoadTraitTextEdits(&error)) {\n      g_autoFinished = true;\n      if (!error.empty())\n        AddLog(u8"[기재 문구/Step6] 저장값 자동 로드 실패: %s", error.c_str());\n      return;\n    }\n    g_autoLoaded = true;\n    g_loadedOnce = true;\n    if (!HasTraitTextEdits()) {\n      g_autoFinished = true;\n      return;\n    }\n  }\n\n  const ULONGLONG now = GetTickCount64();\n  if (g_autoFirstTick == 0) {\n    g_autoFirstTick = now;\n    return;\n  }\n\n  // D3D/메뉴가 뜬 뒤 3초를 기다리고, 이후 최대 30회만 재시도합니다.\n  if (now - g_autoFirstTick < 3000ull || now - g_autoLastAttempt < 1000ull)\n    return;\n  g_autoLastAttempt = now;\n  ++g_autoAttempts;\n\n  std::string error;\n  if (ApplyAll(error)) {\n    g_autoFinished = true;\n    AddLog(u8"[기재 문구/Step6] 저장된 이름/설명 자동 적용 완료");\n    return;\n  }\n\n  if (g_autoAttempts >= 30) {\n    g_autoFinished = true;\n    AddLog(u8"[기재 문구/Step6] 자동 적용 중단(30회 실패): %s", error.c_str());\n  }\n}\n\nvoid DrawTraitTextEditorWindow(float scale) {\n'''
if anchor not in s:
    raise SystemExit('draw anchor missing')
s = s.replace(anchor, tick, 1)
p.write_text(s, encoding='utf-8-sig')

# MenuSections: run the tick every rendered frame before drawing the optional editor window.
p = Path('Internal DX11 Base/MenuSections.cpp')
s = p.read_text(encoding='utf-8-sig')
old = '      EndSection(); // 무장 정보\n      DX11Base::DrawTraitTextEditorWindow(scale);\n'
new = '      EndSection(); // 무장 정보\n      DX11Base::TickTraitTextEditorAutoApply();\n      DX11Base::DrawTraitTextEditorWindow(scale);\n'
if old not in s:
    raise SystemExit('menu editor draw anchor missing')
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8-sig')
