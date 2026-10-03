#pragma once

namespace DX11Base {
  bool ReadMissionAppointmentLimit(int &value);
  bool SetMissionAppointmentLimit(int value);
  bool RestoreMissionAppointmentLimit();
  bool IsMissionAppointmentLimitManaged();
} // namespace DX11Base
