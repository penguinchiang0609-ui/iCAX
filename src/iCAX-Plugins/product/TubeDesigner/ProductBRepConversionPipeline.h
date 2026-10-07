#pragma once

#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <BRepBuilderAPI_Copy.hxx>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace iCAX::TubeDesigner::detail
{
    // Only the submitting caller touches source topology. Meshing owns a deep
    // topology/geometry copy and cannot alter the evaluator's shared prototypes.
    // One worker overlaps CAD with conversion: at most two queued private
    // copies plus its one active copy (and a temporary caller-side copy).
    class CProductBRepConversionPipeline final
    {
    public:
        using Model = iCAX::GeometryData::BRepModel;
        using Input = iCAX::OpenCascade::SBRepConversionInput;
        using Converter = std::function<Model(const TopoDS_Shape&, const std::string&,
            const std::string&, double)>;

        explicit CProductBRepConversionPipeline(std::size_t Count_, double Tolerance_,
            Converter Convert_ = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep)
            : m_Inputs(Count_), m_Models(Count_), m_PrototypeByInput(Count_, InvalidIndex),
              m_Submitted(Count_, false), m_Tolerance(Tolerance_), m_Convert(std::move(Convert_)),
              m_Worker([this] { Work(); })
        {
        }

        CProductBRepConversionPipeline(const CProductBRepConversionPipeline&) = delete;
        CProductBRepConversionPipeline& operator=(const CProductBRepConversionPipeline&) = delete;

        ~CProductBRepConversionPipeline() noexcept
        {
            // On caller/Boolean/contact failure, discard queued work but finish
            // the active private conversion before any captured state is freed.
            {
                const std::lock_guard _Lock(m_Mutex);
                m_Closed = true;
                m_Cancelled = true;
                m_Queue.clear();
            }
            m_Changed.notify_all();
            if (m_Worker.joinable()) m_Worker.join();
        }

        void Submit(std::size_t Index_, const Input& Input_)
        {
            if (Index_ >= m_Inputs.size() || m_Submitted[Index_])
                throw std::logic_error("product BRep conversion slot was submitted twice or is invalid");
            if (Input_.Shape.IsNull()) throw std::invalid_argument("product BRep conversion input is null");
            {
                const std::lock_guard _Lock(m_Mutex);
                RethrowFailure();
                if (m_Closed) throw std::logic_error("product BRep conversion pipeline is closed");
            }
            m_Inputs[Index_] = Input_;
            m_Submitted[Index_] = true;
            const auto _Existing = std::find_if(m_UniqueInputs.begin(), m_UniqueInputs.end(),
                [&](std::size_t Prototype_) { return m_Inputs[Prototype_].Shape.IsEqual(Input_.Shape); });
            if (_Existing != m_UniqueInputs.end()) {
                m_PrototypeByInput[Index_] = *_Existing;
                return;
            }
            m_PrototypeByInput[Index_] = Index_;
            m_UniqueInputs.push_back(Index_);
            const auto _WaitStart = Clock::now();
            {
                std::unique_lock _Lock(m_Mutex);
                m_Changed.wait(_Lock, [&] { return m_Failure || m_Closed || m_Queue.size() < QueueCapacity; });
                RethrowFailure();
                if (m_Closed) throw std::logic_error("product BRep conversion pipeline is closed");
            }
            m_BackpressureMs += Milliseconds(_WaitStart);
            const auto _CopyStart = Clock::now();
            auto _PrivateShape = BRepBuilderAPI_Copy(Input_.Shape, true, false).Shape();
            m_CopyMs += Milliseconds(_CopyStart);
            if (_PrivateShape.IsNull()) throw std::runtime_error("product BRep private copy is null");
            {
                const std::lock_guard _Lock(m_Mutex);
                RethrowFailure();
                if (m_Closed) throw std::logic_error("product BRep conversion pipeline is closed");
                m_Queue.push_back({Index_, std::move(_PrivateShape), Input_.DisplayName, Input_.SourceID});
                m_PeakQueued = std::max(m_PeakQueued, m_Queue.size());
            }
            m_Changed.notify_all();
        }

        std::vector<Model> Finish()
        {
            const auto _Started = Clock::now();
            {
                const std::lock_guard _Lock(m_Mutex);
                m_Closed = true;
            }
            m_Changed.notify_all();
            if (m_Worker.joinable()) m_Worker.join();
            m_FinishWaitMs = Milliseconds(_Started);
            RethrowFailure();
            if (std::find(m_Submitted.begin(), m_Submitted.end(), false) != m_Submitted.end())
                throw std::logic_error("product BRep conversion did not receive every slot");
            // Use the same exact shape identity, ordering and metadata expansion
            // as the existing batch converter, regardless of completion order.
            for (std::size_t _Index = 0; _Index < m_Models.size(); ++_Index) {
                const auto _Prototype = m_PrototypeByInput[_Index];
                if (_Prototype == _Index) continue;
                m_Models[_Index] = m_Models[_Prototype];
                Retag(m_Models[_Index], m_Inputs[_Index]);
            }
            return std::move(m_Models);
        }

        double CopyMilliseconds() const noexcept { return m_CopyMs; }
        double BackpressureMilliseconds() const noexcept { return m_BackpressureMs; }
        double ConversionMilliseconds() const noexcept { return m_ConversionMs; }
        double FinishWaitMilliseconds() const noexcept { return m_FinishWaitMs; }
        std::size_t PeakQueued() const noexcept { return m_PeakQueued; }
        std::size_t UniqueInputs() const noexcept { return m_UniqueInputs.size(); }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr std::size_t InvalidIndex = std::numeric_limits<std::size_t>::max();
        static constexpr std::size_t QueueCapacity = 2;
        struct Job final { std::size_t Index; TopoDS_Shape Shape; std::string Name, SourceID; };

        static double Milliseconds(const Clock::time_point& Start_)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - Start_).count();
        }

        static void Retag(Model& Model_, const Input& Input_)
        {
            Model_.Metadata.Name = Input_.DisplayName;
            Model_.Metadata.SourceId = Input_.SourceID;
            const auto _Rows = [&](auto& Rows_) {
                for (auto& _Row : Rows_) _Row.Metadata.SourceId = Input_.SourceID;
            };
            _Rows(Model_.Curves2); _Rows(Model_.Curves3); _Rows(Model_.Surfaces3);
            _Rows(Model_.Triangulations3); _Rows(Model_.Vertices); _Rows(Model_.Edges);
            _Rows(Model_.Wires); _Rows(Model_.Faces); _Rows(Model_.Shells);
            _Rows(Model_.Solids); _Rows(Model_.CompSolids); _Rows(Model_.Compounds);
        }

        void RethrowFailure() const { if (m_Failure) std::rethrow_exception(m_Failure); }

        void Work() noexcept
        {
            try {
                while (true) {
                    Job _Job;
                    {
                        std::unique_lock _Lock(m_Mutex);
                        m_Changed.wait(_Lock, [&] { return m_Cancelled || m_Closed || !m_Queue.empty(); });
                        if (m_Cancelled || (m_Closed && m_Queue.empty())) return;
                        _Job = std::move(m_Queue.front());
                        m_Queue.pop_front();
                    }
                    m_Changed.notify_all();
                    const auto _Started = Clock::now();
                    m_Models[_Job.Index] = m_Convert(_Job.Shape, _Job.Name, _Job.SourceID, m_Tolerance);
                    m_ConversionMs += Milliseconds(_Started);
                }
            } catch (...) {
                {
                    const std::lock_guard _Lock(m_Mutex);
                    m_Failure = std::current_exception();
                    m_Closed = true;
                    m_Cancelled = true;
                    m_Queue.clear();
                }
                m_Changed.notify_all();
            }
        }

        std::vector<Input> m_Inputs;
        std::vector<Model> m_Models;
        std::vector<std::size_t> m_PrototypeByInput, m_UniqueInputs;
        std::vector<bool> m_Submitted;
        double m_Tolerance;
        Converter m_Convert;
        std::mutex m_Mutex;
        std::condition_variable m_Changed;
        std::deque<Job> m_Queue;
        std::exception_ptr m_Failure;
        bool m_Closed = false, m_Cancelled = false;
        double m_CopyMs = 0, m_BackpressureMs = 0, m_ConversionMs = 0, m_FinishWaitMs = 0;
        std::size_t m_PeakQueued = 0;
        std::thread m_Worker;
    };
}
