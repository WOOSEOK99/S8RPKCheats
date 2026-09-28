#pragma once

namespace DX11Base {

// 0: disabled, 1: ruler's direct advisor only, 2: direct advisor + governor
extern int iRulerTransferProposalMode;

bool SetRulerTransferProposalMode(int mode);
int GetRulerTransferProposalMode();
bool IsRulerTransferProposalApplied();
bool SaveRulerTransferProposalPreference(bool enabled);
bool LoadRulerTransferProposalPreference();

} // namespace DX11Base
