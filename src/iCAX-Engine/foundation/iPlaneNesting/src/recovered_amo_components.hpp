#pragma once
#include "recovered_amo_shape.hpp"
namespace study::recovered {
// 8b4380. A finalized child contributes every contour only when neither
// invalid-input flag is set; the parent's properties remain its own.
void amoTilingAttachComponent(AmoTilingTransformedShape &,const AmoTilingTransformedShape &);
}
