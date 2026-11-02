#include "pch.h"
#include "OpenCascadeVisibleImport.h"
#include "OpenCascadeCancellation.h"
#include "OpenCascadeIgesSession.h"
#include <BRep_Builder.hxx>
#include <StepVisual_Invisibility.hxx>
#include <StepVisual_InvisibleItem.hxx>
#include <StepVisual_PresentationRepresentation.hxx>
#include <StepVisual_StyledItem.hxx>
#include <TDocStd_Document.hxx>
#include <TDF_ChildIterator.hxx>
#include <NCollection_Map.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_LayerTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XSControl_WorkSession.hxx>
#include <XSControl_TransferReader.hxx>
#include <UnitsMethods.hxx>

namespace iCAX::OpenCascade
{
    std::unique_lock<std::timed_mutex> AcquireCadReadParameterLock()
    {
        static std::timed_mutex mutex;
        std::unique_lock lock(mutex, std::defer_lock);
        while (!lock.try_lock_for(std::chrono::milliseconds(25)))
            COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        return lock;
    }

    namespace
    {
        using ShapeSet = NCollection_Map<TopoDS_Shape, TopTools_ShapeMapHasher>;

        void ExpandHiddenPresentations(STEPControl_Reader& reader)
        {
            // OCCT reads invisible styled items and layers, but does not expand
            // an invisible presentation representation to its member styles.
            // Normalize that standard grouping in this transient read model;
            // the frozen source bytes and source resource remain untouched.
            const auto model = reader.Model();
            bool changed = false;
            for (int i = 1; i <= model->NbEntities(); ++i)
            {
                COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                const auto invisible = occ::down_cast<StepVisual_Invisibility>(model->Value(i));
                if (invisible.IsNull() || invisible->InvisibleItems().IsNull()) continue;
                std::vector<StepVisual_InvisibleItem> members;
                for (int j = 1; j <= invisible->NbInvisibleItems(); ++j)
                {
                    const auto item = invisible->InvisibleItemsValue(j);
                    members.push_back(item);
                    const auto presentation = item.PresentationRepresentation();
                    if (presentation.IsNull() || presentation->Items().IsNull()) continue;
                    for (int k = 1; k <= presentation->NbItems(); ++k)
                    {
                        const auto style = occ::down_cast<StepVisual_StyledItem>(presentation->ItemsValue(k));
                        if (style.IsNull()) continue;
                        StepVisual_InvisibleItem member; member.SetValue(style); members.push_back(member);
                    }
                }
                if (members.size() == invisible->NbInvisibleItems()) continue;
                occ::handle<NCollection_HArray1<StepVisual_InvisibleItem>> expanded =
                    new NCollection_HArray1<StepVisual_InvisibleItem>(1, static_cast<int>(members.size()));
                for (int j = 0; j < static_cast<int>(members.size()); ++j) expanded->SetValue(j + 1, members[j]);
                invisible->SetInvisibleItems(expanded);
                changed = true;
            }
            if (changed) reader.WS()->ComputeGraph(true);
        }
        void CheckDepth(int depth)
        {
            COpenCascadeCancellationScope::ThrowIfCancellationRequested();
            if (depth > 256)
                throw std::runtime_error("CAD_VISIBILITY_DEPTH: source hierarchy exceeds import budget");
        }

        // Only containers are rebuilt. A visible face still needs all of its
        // trimming wires/edges, even if their independent presentation is hidden.
        TopoDS_Shape FilterGeometry(const TopoDS_Shape& source, const TopLoc_Location& parent,
                                   const ShapeSet& hidden, int depth)
        {
            CheckDepth(depth);
            if (hidden.Contains(source.Moved(parent))) return {};
            if (source.ShapeType() >= TopAbs_FACE) return source;
            const auto location = parent * source.Location();
            std::vector<TopoDS_Shape> children;
            bool changed = false;
            for (TopoDS_Iterator it(source, false, false); it.More(); it.Next())
            {
                auto child = FilterGeometry(it.Value(), location, hidden, depth + 1);
                changed |= child.IsNull() || !child.IsEqual(it.Value());
                if (!child.IsNull()) children.push_back(child);
            }
            if (!changed) return source;
            if (children.empty()) return {};
            BRep_Builder builder;
            TopoDS_Shape result;
            if (source.ShapeType() == TopAbs_SHELL)
            {
                TopoDS_Shell shell; builder.MakeShell(shell); result = shell;
            }
            else
            {
                // Removing hidden faces from a solid yields a surface model,
                // never an invalid solid that still claims to enclose a volume.
                TopoDS_Compound compound; builder.MakeCompound(compound); result = compound;
            }
            for (const auto& child : children) builder.Add(result, child);
            result.Location(source.Location());
            result.Orientation(source.Orientation());
            return result;
        }

        class CVisibilitySelection final
        {
        public:
            explicit CVisibilitySelection(const occ::handle<TDocStd_Document>& document)
                : Shapes(XCAFDoc_DocumentTool::ShapeTool(document->Main())),
                  Colors(XCAFDoc_DocumentTool::ColorTool(document->Main())),
                  Layers(XCAFDoc_DocumentTool::LayerTool(document->Main())) {}

            TopoDS_Shape Filter(const TDF_Label& label, const TopLoc_Location& parent = {}, int depth = 0)
            {
                CheckDepth(depth);
                const auto source = Shapes->GetShape(label);
                if (source.IsNull()) return {};
                if (!Visible(label) || !Colors->IsInstanceVisible(source.Moved(parent)))
                {
                    ++HiddenLabels;
                    return {};
                }
                TDF_Label referred;
                if (Shapes->GetReferredShape(label, referred))
                {
                    const auto location = Shapes->GetLocation(label);
                    auto result = Filter(referred, parent * location, depth + 1);
                    return result.IsNull() ? result : result.Moved(location);
                }
                if (Shapes->IsAssembly(label))
                {
                    NCollection_Sequence<TDF_Label> components;
                    Shapes->GetComponents(label, components);
                    BRep_Builder builder;
                    TopoDS_Compound compound; builder.MakeCompound(compound);
                    bool changed = false;
                    int retained = 0;
                    for (const auto& component : components)
                    {
                        const auto original = Shapes->GetShape(component);
                        auto child = Filter(component, parent * source.Location(), depth + 1);
                        changed |= child.IsNull() || !child.IsEqual(original);
                        if (!child.IsNull()) { builder.Add(compound, child); ++retained; }
                    }
                    if (!changed) return source;
                    if (!retained) return {};
                    compound.Location(source.Location());
                    compound.Orientation(source.Orientation());
                    return compound;
                }
                ShapeSet hidden;
                CollectHiddenSubshapes(label, true, hidden, depth + 1);
                return hidden.IsEmpty() ? source : FilterGeometry(source, {}, hidden, depth + 1);
            }

            int HiddenLabels = 0;
            occ::handle<XCAFDoc_ShapeTool> Shapes;
        private:
            bool Visible(const TDF_Label& label) const
            {
                if (!Colors->IsVisible(label)) return false;
                NCollection_Sequence<TDF_Label> layers;
                Layers->GetLayers(label, layers);
                // A shape assigned to several layers remains visible while at
                // least one of those layers is visible (XDE display semantics).
                if (layers.IsEmpty()) return true;
                for (const auto& layer : layers) if (Layers->IsVisible(layer)) return true;
                return false;
            }

            void CollectHiddenSubshapes(const TDF_Label& parent, bool inheritedVisible,
                                       ShapeSet& hidden, int depth)
            {
                CheckDepth(depth);
                for (TDF_ChildIterator it(parent, false); it.More(); it.Next())
                {
                    const auto label = it.Value();
                    const bool visible = inheritedVisible && Visible(label);
                    const auto shape = Shapes->GetShape(label);
                    if (!visible && !shape.IsNull()) { hidden.Add(shape); ++HiddenLabels; }
                    CollectHiddenSubshapes(label, visible, hidden, depth + 1);
                }
            }

            occ::handle<XCAFDoc_ColorTool> Colors;
            occ::handle<XCAFDoc_LayerTool> Layers;
        };
    }

    SVisibleStepTransfer TransferVisibleStep(STEPCAFControl_Reader& caf,
                                            const Message_ProgressRange& progress)
    {
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        auto& reader = caf.ChangeReader();
        const auto expected = reader.NbRootsForTransfer();
        if (expected <= 0) throw std::runtime_error("CAD_EMPTY_GEOMETRY: no source geometry");
        bool hasVisibility = false;
        for (int i = 1; i <= reader.Model()->NbEntities(); ++i)
        {
            COpenCascadeCancellationScope::ThrowIfCancellationRequested();
            if (reader.Model()->Value(i)->IsKind(STANDARD_TYPE(StepVisual_Invisibility)))
            { hasVisibility = true; break; }
        }
        SVisibleStepTransfer result;
        if (!hasVisibility)
        {
            result.TransferredRoots = reader.TransferRoots(progress);
            result.Shape = reader.OneShape();
        }
        else
        {
            ExpandHiddenPresentations(reader);
            // XDE reads element styles, layer visibility and occurrence/SHUO
            // inheritance. Ordinary geometry-only transfer loses all of these.
            occ::handle<TDocStd_Document> document = new TDocStd_Document("BinXCAF");
            XCAFDoc_DocumentTool::SetLengthUnit(document, 1.0, UnitsMethods_LengthUnit_Millimeter);
            caf.SetColorMode(true); caf.SetLayerMode(true); caf.SetSHUOMode(true);
            caf.SetNameMode(false); caf.SetPropsMode(false); caf.SetGDTMode(false);
            caf.SetMatMode(false); caf.SetViewMode(false);
            if (!caf.Transfer(document, progress))
            {
                COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                throw std::runtime_error("CAD_TRANSFER_INCOMPLETE: STEP presentation transfer failed");
            }
            for (int i = 1; i <= expected; ++i)
                if (!reader.WS()->TransferReader()->ShapeResult(reader.RootForTransfer(i)).IsNull())
                    ++result.TransferredRoots;
            CVisibilitySelection selection(document);
            NCollection_Sequence<TDF_Label> roots;
            selection.Shapes->GetFreeShapes(roots);
            BRep_Builder builder;
            TopoDS_Compound compound; builder.MakeCompound(compound);
            std::vector<TopoDS_Shape> retained;
            for (const auto& root : roots)
            {
                auto shape = selection.Filter(root);
                if (!shape.IsNull()) retained.push_back(shape);
            }
            result.HiddenLabels = selection.HiddenLabels;
            if (!selection.HiddenLabels) result.Shape = reader.OneShape();
            else if (retained.size() == 1) result.Shape = retained.front();
            else if (!retained.empty())
            {
                for (const auto& shape : retained) builder.Add(compound, shape);
                result.Shape = compound;
            }
        }
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        if (result.TransferredRoots != expected)
            throw std::runtime_error("CAD_TRANSFER_INCOMPLETE: transferred "
                + std::to_string(result.TransferredRoots) + " of " + std::to_string(expected) + " STEP roots");
        if (result.Shape.IsNull())
            throw std::runtime_error("CAD_EMPTY_VISIBLE_GEOMETRY: source contains no visible surface or solid geometry");
        return result;
    }
}
