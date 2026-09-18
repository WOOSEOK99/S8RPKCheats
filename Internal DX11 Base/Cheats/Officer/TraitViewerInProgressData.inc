static constexpr char kInProgressCaveB64[] =
  "SImQkAAAAEiJkJgAAADpAAAAAEiJ+UiNleALAABEi4XgCwAARCuF0AsAAEWJ6eg9AAAASIsFAAAAAOkAAAAASIPsOEiJTCQg6BkAAABIiUQkKEiLTCQg6DgB"
  "AABIi0QkKEiDxDjDQFNVQVXpAAAAAFNWV1VBVEiD7GBIictIidZEicVFicxIx4NgAgAAAAAAAOgAAAAASIuIGAEAAMdEJEA1AAAASMdEJEgAAAAATI1EJEC6"
  "uAEAAEiLAf9QKEiFwA+EwgAAAEiJx0iJwegAAAAASI0FAAAAAEiJB8eHoAEAAAAAAABmx4ekAQAAAADHh6gBAAD/////SMeHsAEAAAAAAABIibtgAgAASIn5"
  "RIniRIsGQQHoRItOBItGCIlEJCBIiVwkKMZEJDABxkQkOABIiwf/kEACAADoAAAAAEiLSCjHRCRANwAAAEjHRCRIAAAAAEyNRCRAuhAAAABIiwH/UChIhcB0"
  "IEjHAAAAAABIiXgISInCSI2LQAEAAEG4AQAAAOgAAAAASIPEYEFcXV9eW8NAU1VBVUFWSIPsKEiJdCRYTIvpSIl8JGC9AgAAAEyJfCQgQb6YAAAASYudYAIA"
  "AEiF2w+ENgEAAIP9Aw+CgAAAADP/M9JAMvZIiwNIi8v/kAgBAABIibuwAQAAQIT2dFhIi8/oAAAAAEiLi0gBAABIi/BMi3kQ6AAAAABFM8BIi85IhfZ0GWZE"
  "OQZ0E2YPH0QAAEiNSQJB/8BmgzkAdfNJiwdIi9ZJi8//kMABAAAPt0cIiYOoAQAAg/0DcjYz2+s9SYuFqAEAAEmLPAZIhf8PhG7///9IiwdIi8//UEiEwA+E"
  "Xf///0C2AboBAAAA6VX///9Ji4WoAQAASYscBkmLtWACAABIhdt0ZEiLA0iLy/9QSITAdFcPtksuM/+D6QF0IoPpAXQRg/kBdSq6PAMAAL+QAAAA6xa6OwMA"
  "AL+PAAAA6wq6OgMAAL+FAAAASIvO6AAAAACF/3QUSI2OQAEAAESLx7oCAAAA6AAAAAD/xUmDxgiD/QMPjKv+//9Mi3wkIEiLfCRgSIt0JFhIg8QoQV5BXV1b"
  "ww==";
static constexpr Reloc kInProgressRelocs[] = {
  {0x36,0x2E99C98},{0x92,0x14ED0},{0xCE,0x1EDE250},{0xD5,0x25EE270},{0x13B,0x14ED0},{0x185,0x1D0D250},
  {0x1F8,0x170E2D0},{0x20B,0x14C70},{0x2E6,0x162C650},{0x2FE,0x1D17280},{0xF,0x12B67F5},{0x3B,0x12B9C4F},{0x6C,0x12B7035},
};
static constexpr AssertSpec kInProgressEnableAsserts[] = {
  {0x12B67EE,"48899090000000"},{0x12B5F7A,"BA60020000"},{0x12B9C48,"488B054900BE01"},{0x12B7030,"4053554155"},
  {0x12E273A,"83FF02"},{0x12E280E,"C7458402000000"},{0x12E2A33,"83FA02"},{0x12E2A41,"83FA02"},{0x12E2A6D,"83FA02"},
};
static constexpr AssertSpec kInProgressDisableAsserts[] = {
  {0x12E273A,"83FF03"},{0x12E280E,"C7458403000000"},{0x12E2A33,"83FA03"},{0x12E2A41,"83FA03"},{0x12E2A6D,"83FA03"},
};
static constexpr PatchSpec kInProgressEnablePatches[] = {
  {0x12B5F7A,PatchKind::Bytes,"BA70020000",0,0},
  {0x12B9C48,PatchKind::JumpCave,nullptr,7,0x13},
  {0x12B7030,PatchKind::JumpCave,nullptr,5,0x3F},
  {0x12B67EE,PatchKind::JumpCave,nullptr,7,0x0},
  {0x12E273C,PatchKind::Bytes,"03",0,0},{0x12E2811,PatchKind::Bytes,"03",0,0},{0x12E2A35,PatchKind::Bytes,"03",0,0},
  {0x12E2A43,PatchKind::Bytes,"03",0,0},{0x12E2A6F,PatchKind::Bytes,"03",0,0},
};
static constexpr PatchSpec kInProgressDisablePatches[] = {
  {0x12E273C,PatchKind::Bytes,"02",0,0},{0x12E2811,PatchKind::Bytes,"02",0,0},{0x12E2A35,PatchKind::Bytes,"02",0,0},
  {0x12E2A43,PatchKind::Bytes,"02",0,0},{0x12E2A6F,PatchKind::Bytes,"02",0,0},
  {0x12B67EE,PatchKind::Bytes,"48899090000000",0,0},{0x12B5F7A,PatchKind::Bytes,"BA60020000",0,0},
  {0x12B9C48,PatchKind::Bytes,"488B054900BE01",0,0},{0x12B7030,PatchKind::Bytes,"4053554155",0,0},
};
static constexpr BlockSpec kInProgressSpec = {"CT 91512",0x12B67EE,0x8192,kInProgressCaveB64,kInProgressRelocs,CountOf(kInProgressRelocs),kInProgressEnableAsserts,CountOf(kInProgressEnableAsserts),kInProgressDisableAsserts,CountOf(kInProgressDisableAsserts),kInProgressEnablePatches,CountOf(kInProgressEnablePatches),kInProgressDisablePatches,CountOf(kInProgressDisablePatches)};


static constexpr char kInProgressCancelFixCaveB64[] =
  "x4J0AwAA/////0iLgTgCAABIi5GoAQAASImCmAAAAMeCdAMAAP/////pAAAAAMzMzMzMzMzMzMzMzMzMzEiLgpAAAABIOYEwAgAAD4UAAAAASIuCmAAAAEg5gTgCAAAPhQAAAADpAAAAAA==";

static constexpr Reloc kInProgressCancelFixRelocs[] = {
  {0x2A,0x12B69D7},
  {0x50,0x12B6C9A},
  {0x64,0x12B6C9A},
  {0x69,0x12B6BBF},
};

static constexpr AssertSpec kInProgressCancelFixEnableAsserts[] = {
  // 취소 복원: 슬롯2 복원 직후의 dirty 플래그 갱신 지점.
  {0x12B69CD,"C78274030000FFFFFFFF"},
  // 변경 감지: 슬롯2 비교 시작 지점.
  {0x12B6BAB,"488B8290000000"},
  // 편집창 진입 시 슬롯3 원본은 이미 editor+0x238에 백업됨.
  {0x12BA1A2,"488B8898000000"},
  {0x12BA1A9,"48898F38020000"},
};

static constexpr PatchSpec kInProgressCancelFixEnablePatches[] = {
  {0x12B69CD,PatchKind::JumpCave,nullptr,10,0x00},
  {0x12B6BAB,PatchKind::JumpCave,nullptr,7,0x40},
};

static constexpr PatchSpec kInProgressCancelFixDisablePatches[] = {
  {0x12B6BAB,PatchKind::Bytes,"488B8290000000",0,0},
  {0x12B69CD,PatchKind::Bytes,"C78274030000FFFFFFFF",0,0},
};

static constexpr BlockSpec kInProgressCancelFixSpec = {
  "CT 91512 cancel-fix",
  0x12B69CD,
  0x1000,
  kInProgressCancelFixCaveB64,
  kInProgressCancelFixRelocs,
  CountOf(kInProgressCancelFixRelocs),
  kInProgressCancelFixEnableAsserts,
  CountOf(kInProgressCancelFixEnableAsserts),
  nullptr,
  0,
  kInProgressCancelFixEnablePatches,
  CountOf(kInProgressCancelFixEnablePatches),
  kInProgressCancelFixDisablePatches,
  CountOf(kInProgressCancelFixDisablePatches)
};
