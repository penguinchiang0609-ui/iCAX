#pragma once

#include "OpenCascadeCancellation.h"
#include <IGESControl_Controller.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Controller.hxx>
#include <DESTEP_Parameters.hxx>
#include <chrono>
#include <mutex>
#include <stdexcept>

namespace iCAX::OpenCascade
{
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::unique_lock<std::timed_mutex> AcquireCadReadParameterLock();

    // STEP carries a per-model copy of the read parameters. Snapshot it while
    // no IGES session has installed its temporary 3D preference; file parsing
    // and the potentially long STEP transfer then run without this lock.
    inline DESTEP_Parameters CaptureStepReadParameters()
    {
        const auto lock = AcquireCadReadParameterLock();
        STEPControl_Controller::Init();
        DESTEP_Parameters parameters;
        parameters.InitFromStatic();
        return parameters;
    }

    // OCCT's IGESFile parser owns process-global buffers, and the registered
    // IGESControl_Controller reuses an actor whose model changes in ActorRead.
    // Keep construction, parsing, transfer and reader destruction in one session.
    // Both import entry points in this module share this mutex. Waiting stays
    // on the caller's worker and observes cancellation without blocking Scene.
    class CIgesImportSession final
    {
    public:
        CIgesImportSession() : m_Lock(AcquireCadReadParameterLock())
        {
            IGESControl_Controller::Init();
            m_PreviousSurfaceCurveMode = Interface_Static::IVal("read.surfacecurve.mode");
            // Prefer the source 3D boundary when IGES supplies both 2D and 3D.
            // A polynomial UV approximation must not replace an exact 3D arc.
            // Preferred mode still permits files that only carry a UV boundary.
            if (!Interface_Static::SetIVal("read.surfacecurve.mode", 3))
                throw std::runtime_error("Cannot set the IGES 3D boundary preference");
        }

        ~CIgesImportSession()
        {
            Interface_Static::SetIVal("read.surfacecurve.mode", m_PreviousSurfaceCurveMode);
        }

        CIgesImportSession(const CIgesImportSession&) = delete;
        CIgesImportSession& operator=(const CIgesImportSession&) = delete;

    private:
        std::unique_lock<std::timed_mutex> m_Lock;
        int m_PreviousSurfaceCurveMode = 0;
    };

    inline CIgesImportSession AcquireIgesSession()
    {
        return CIgesImportSession{};
    }
}
