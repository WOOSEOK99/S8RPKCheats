#pragma once
#include <vector>
#include <string>

struct CityInfo {
    const char* cityname;
    // 이제 주소(addr) 필드는 필요하지 않습니다. 인덱스 기반으로 동적 계산됩니다.
};

static const CityInfo g_CityList[] = {
    { u8"양평" }, { u8"북평" }, { u8"계" }, { u8"남피" },
    { u8"업" }, { u8"평원" }, { u8"북해" }, { u8"제남" },
    { u8"진양" }, { u8"상당" }, { u8"하비" }, { u8"소패" },
    { u8"복양" }, { u8"진류" }, { u8"허창" }, { u8"낙양" },
    { u8"홍농" }, { u8"장안" }, { u8"안정" }, { u8"천수" },
    { u8"서량" }, { u8"무도" }, { u8"한중" }, { u8"수준" },
    { u8"건업" }, { u8"오" }, { u8"회계" }, { u8"건안" },
    { u8"여강" }, { u8"시상" }, { u8"신야" }, { u8"양양" },
    { u8"상용" }, { u8"강릉" }, { u8"장사" }, { u8"무릉" },
    { u8"계양" }, { u8"영릉" }, { u8"성도" }, { u8"자중" },
    { u8"부" }, { u8"강주" }, { u8"영안" }, { u8"건녕" },
    { u8"운남" }, { u8"영창" }, { u8"강하" }, { u8"완" },
    { u8"자동" }, { u8"강변" }, { u8"교지" }
};

static const int g_CityCount = sizeof(g_CityList) / sizeof(CityInfo);
static int s_selectedCityIdx = 0;
