from pathlib import Path

path = Path('Internal DX11 Base/MenuSections.cpp')
text = path.read_text(encoding='utf-8-sig')

fast_block = '''      ImGui::SameLine(160.0f * scale);\n\n      if (ImGui::Checkbox(u8"경애 시 무조건 공명", &bFastRelationship)) {\n        DX11Base::SetFastRelationship(bFastRelationship);\n        NotifyFeatureToggle(u8"경애 시 무조건 공명", bFastRelationship);\n        SaveConfig();\n      }\n\n'''
if fast_block in text:
    text = text.replace(fast_block, '      ImGui::SameLine(160.0f * scale);\n', 1)
else:
    raise SystemExit('fast relationship UI block not found')

text = text.replace('''      ImGui::SameLine(160.0f * scale);\n\n      if (ImGui::Checkbox(u8"담화 실행 무제한", &bInfiniteTalk)) {''', '''      if (ImGui::Checkbox(u8"담화 실행 무제한", &bInfiniteTalk)) {''', 1)

text = text.replace('''      if (::DX11Base::g_loyaltyThreadRunning.load())\n        ImGui::BeginDisabled();''', '''      ImGui::SameLine(160.0f * scale);\n      if (::DX11Base::g_loyaltyThreadRunning.load())\n        ImGui::BeginDisabled();''', 1)

text = text.replace('''      ImGui::SameLine(160.0f * scale);\n      bool tempMarriage = ::DX11Base::marriageApplied;''', '''      bool tempMarriage = ::DX11Base::marriageApplied;''', 1)

path.write_text(text, encoding='utf-8-sig')
