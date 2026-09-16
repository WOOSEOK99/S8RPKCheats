from pathlib import Path

# MenuSections.cpp
p = Path('Internal DX11 Base/MenuSections.cpp')
s = p.read_text(encoding='utf-8-sig')
a = '#include "Cheats/Officer/OfficerDetail.h"\n'
inc = '#include "Cheats/Officer/TraitViewerFeature.h"\n'
if inc not in s:
    if a not in s:
        raise SystemExit('MenuSections include anchor missing')
    s = s.replace(a, a + inc, 1)
old = '      if (ImGui::Checkbox(u8"기재 화면 보이기", &DX11Base::bTraitViewer)) {\n        NotifyFeatureToggle(u8"기재 화면 보이기", DX11Base::bTraitViewer);\n        SaveConfig();\n      }\n'
new = '      if (ImGui::Checkbox(u8"기재 화면 보이기", &DX11Base::bTraitViewer)) {\n        const bool requested = DX11Base::bTraitViewer;\n        if (!DX11Base::SetTraitViewerFeature(requested))\n          DX11Base::bTraitViewer = DX11Base::IsTraitViewerFeatureApplied();\n        NotifyFeatureToggle(u8"기재 화면 보이기", DX11Base::bTraitViewer);\n        SaveConfig();\n      }\n'
if old in s:
    s = s.replace(old, new, 1)
elif new not in s:
    raise SystemExit('MenuSections checkbox anchor missing')
p.write_text(s, encoding='utf-8-sig')

# Config.cpp
p = Path('Internal DX11 Base/Config.cpp')
s = p.read_text(encoding='utf-8')
inc = '#include "Cheats/Officer/TraitViewerFeature.h"\n\n'
a = '#undef SaveConfig\n\n'
if inc not in s:
    if a not in s:
        raise SystemExit('Config include anchor missing')
    s = s.replace(a, a + inc, 1)
old = '    // 이전 설정 파일에 키가 없으면 MenuState.cpp의 기본값(true)을 그대로 유지합니다.\n    bool savedTraitViewer = true;\n    if (LoadBoolConfigValue("bTraitViewer", savedTraitViewer)) {\n      bTraitViewer = savedTraitViewer;\n      AddLog(u8"[Config] 기재 화면 보이기 설정 로드: %s", savedTraitViewer ? "ON" : "OFF");\n    }\n'
new = '    // 이전 설정 파일에 키가 없으면 기본값(true)으로 실제 패치까지 적용합니다.\n    bool savedTraitViewer = true;\n    const bool hasTraitViewerSetting = LoadBoolConfigValue("bTraitViewer", savedTraitViewer);\n    bTraitViewer = savedTraitViewer;\n    if (!SetTraitViewerFeature(bTraitViewer))\n      bTraitViewer = IsTraitViewerFeatureApplied();\n    AddLog(u8"[Config] 기재 화면 보이기 설정 로드%s: %s",\n           hasTraitViewerSetting ? "" : "(기본값)", bTraitViewer ? "ON" : "OFF");\n'
if old in s:
    s = s.replace(old, new, 1)
elif new not in s:
    raise SystemExit('Config trait viewer block missing')
p.write_text(s, encoding='utf-8')

# vcxproj
p = Path('Internal DX11 Base/Internal DX11 Base.vcxproj')
s = p.read_text(encoding='utf-8-sig')
h = '    <ClInclude Include="Cheats\\Officer\\TraitViewerFeature.h" />\n'
ha = '    <ClInclude Include="Cheats\\Officer\\TraitViewerBattlePrep.h" />\n'
if 'Cheats\\Officer\\TraitViewerFeature.h' not in s:
    if ha not in s:
        raise SystemExit('vcxproj header anchor missing')
    s = s.replace(ha, ha + h, 1)
c = '    <ClCompile Include="Cheats\\Officer\\TraitViewerFeature.cpp" />\n'
ca = '    <ClCompile Include="Cheats\\Officer\\TraitViewerBattlePrep.cpp" />\n'
if 'Cheats\\Officer\\TraitViewerFeature.cpp' not in s:
    if ca not in s:
        raise SystemExit('vcxproj cpp anchor missing')
    s = s.replace(ca, ca + c, 1)
p.write_text(s, encoding='utf-8-sig')
