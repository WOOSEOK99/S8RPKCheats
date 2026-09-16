#pragma once
#include "helper.h"

// Menu.cpp는 Menu.h 다음에 MenuSections.h를 포함합니다.
// 이 플래그가 정의된 번역 단위에서만 전쟁 섹션 뒤에 확장 UI를 붙입니다.
#define DX11BASE_MENU_WRAP_WAR_SECTION 1

namespace DX11Base 
{
	class Menu
	{
	public:
		static void Render();
		static void Loops();
		static void DrawMenu();

		Menu()  noexcept = default;
		~Menu() noexcept = default;
	};
}