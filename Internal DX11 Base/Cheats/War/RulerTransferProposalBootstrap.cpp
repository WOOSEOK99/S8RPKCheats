#include "RulerTransferProposal.h"

namespace {
struct RulerTransferProposalBootstrap {
  RulerTransferProposalBootstrap() {
    DX11Base::SetRulerTransferProposalMode(2);
  }
};

RulerTransferProposalBootstrap g_rulerTransferProposalBootstrap;
} // namespace
