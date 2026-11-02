#pragma once

#include <STEPCAFControl_Reader.hxx>
#include <Message_ProgressRange.hxx>
#include <TopoDS_Shape.hxx>

namespace iCAX::OpenCascade
{
    struct SVisibleStepTransfer final
    {
        TopoDS_Shape Shape;
        int TransferredRoots = 0;
        int HiddenLabels = 0;
    };

    // Source visibility is an import selection, before bounds, tessellation,
    // topology references and machining recognition are produced.
    SVisibleStepTransfer TransferVisibleStep(STEPCAFControl_Reader& reader,
                                            const Message_ProgressRange& progress = {});
}
