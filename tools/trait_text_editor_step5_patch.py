from pathlib import Path

p = Path('Internal DX11 Base/MenuSections.cpp')
s = p.read_text(encoding='utf-8-sig')
inc = '#include "Cheats/Officer/TraitTextEditorWindow.h"\n'
anchor = '#include "Cheats/Officer/TraitViewerFeature.h"\n'
if inc not in s:
    if anchor not in s:
        raise SystemExit('MenuSections include anchor missing')
    s = s.replace(anchor, anchor + inc, 1)
old = '      ImGui::Spacing();\n      if (ImGui::Checkbox(u8"기재 화면 보이기", &DX11Base::bTraitViewer)) {\n        const bool requested = DX11Base::bTraitViewer;\n        if (!DX11Base::SetTraitViewerFeature(requested))\n          DX11Base::bTraitViewer = DX11Base::IsTraitViewerFeatureApplied();\n        NotifyFeatureToggle(u8"기재 화면 보이기", DX11Base::bTraitViewer);\n        SaveConfig();\n      }\n\n      EndSection(); // 무장 정보\n'
new = '      ImGui::Spacing();\n      if (ImGui::Checkbox(u8"기재 화면 보이기", &DX11Base::bTraitViewer)) {\n        const bool requested = DX11Base::bTraitViewer;\n        if (!DX11Base::SetTraitViewerFeature(requested))\n          DX11Base::bTraitViewer = DX11Base::IsTraitViewerFeatureApplied();\n        NotifyFeatureToggle(u8"기재 화면 보이기", DX11Base::bTraitViewer);\n        SaveConfig();\n      }\n      ImGui::SameLine();\n      if (ImGui::Button(u8"기재 이름 편집", ImVec2(110.0f * scale, 26.0f * scale))) {\n        DX11Base::OpenTraitTextEditorWindow();\n      }\n\n      EndSection(); // 무장 정보\n      DX11Base::DrawTraitTextEditorWindow(scale);\n'
if old in s:
    s = s.replace(old, new, 1)
elif new not in s:
    raise SystemExit('MenuSections trait viewer block missing')
p.write_text(s, encoding='utf-8-sig')

p = Path('Internal DX11 Base/Internal DX11 Base.vcxproj')
s = p.read_text(encoding='utf-8-sig')
h = '    <ClInclude Include="Cheats\\Officer\\TraitTextEditorWindow.h" />\n'
ha = '    <ClInclude Include="Cheats\\Officer\\TraitTextSpecialDescHook.h" />\n'
if 'Cheats\\Officer\\TraitTextEditorWindow.h' not in s:
    if ha not in s:
        raise SystemExit('vcxproj header anchor missing')
    s = s.replace(ha, ha + h, 1)
c = '    <ClCompile Include="Cheats\\Officer\\TraitTextEditorWindow.cpp" />\n'
ca = '    <ClCompile Include="Cheats\\Officer\\TraitTextSpecialDescHook.cpp" />\n'
if 'Cheats\\Officer\\TraitTextEditorWindow.cpp' not in s:
    if ca not in s:
        raise SystemExit('vcxproj cpp anchor missing')
    s = s.replace(ca, ca + c, 1)
p.write_text(s, encoding='utf-8-sig')
