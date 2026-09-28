#include "RulerTransferProposal.h"

namespace {
struct RulerTransferProposalBootstrap {
  RulerTransferProposalBootstrap() {
    DX11Base::LoadRulerTransferProposalPreference();
  }
};

RulerTransferProposalBootstrap g_rulerTransferProposalBootstrap;
} // namespace
