#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    // Bounded vertex-contact search for a one-stock, zero-gap polygon order
    // whose real material area fills the usable board. Failure is not proof.
    StrategyFactory MakeExactFillContactFactory();
}
