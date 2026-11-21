#include "recovered_rich_traversal.hpp"
#include <stdexcept>

namespace study::recovered {
void richVisit(const RichNode &node,const RichVisitor &visitor){
    const auto event=[&](RichEvent e){if(visitor.event)visitor.event(e,node);};
    const auto proceed=[&](){return !visitor.proceed||visitor.proceed();};
    const auto visit=[&](const RichNode *child){if(!child)throw std::invalid_argument("Null rich-geometry child");richVisit(*child,visitor);};
    switch(node.kind){
    case RichKind::Root:
        event(RichEvent::RootBegin);
        for(auto child:node.children){if(!proceed())break;visit(child);}
        if(proceed())event(RichEvent::RootEnd);
        break;
    case RichKind::Polygon:
        event(RichEvent::PolygonBegin);
        if(proceed()&&node.outer)visit(node.outer);
        for(auto child:node.holes){if(!proceed())break;visit(child);}
        for(auto child:node.auxiliary){if(!proceed())break;visit(child);}
        if(proceed())event(RichEvent::PolygonEnd);
        break;
    case RichKind::Curve:{
        event(RichEvent::CurveBegin);auto vertex=node.first;
        while(vertex&&proceed()){
            visit(vertex);
            if(proceed()){
                const auto edge=vertex->next;if(!edge)break;
                vertex=edge->next;visit(edge);
                if(vertex==node.last)break;
            }
        }
        if(proceed()&&node.first!=node.last)visit(vertex);
        if(proceed())event(RichEvent::CurveEnd);
        break;}
    case RichKind::Vertex:event(RichEvent::Vertex);break;
    case RichKind::Edge:event(RichEvent::Edge);event(node.line?RichEvent::Line:RichEvent::OtherCurve);break;
    }
}
size_t shakerAppendVisitedTree(ShakerInputScene &scene,size_t shape,int32_t type,
    const RichNode &root,const RichPolygonArea &area,std::optional<double> offset){
    if(!area)throw std::invalid_argument("Rich polygon area provider is required");
    std::vector<ShakerPoint> points;bool collecting=false;size_t count=0;
    richVisit(root,{[&](RichEvent e,const RichNode &node){
        switch(e){
        case RichEvent::PolygonBegin:collecting=node.outer!=nullptr;points.clear();break;
        case RichEvent::Vertex:if(collecting)points.push_back(node.point);break;
        case RichEvent::CurveEnd:collecting=false;break;
        case RichEvent::PolygonEnd:
            shakerAppendVisitedComponent(scene,shape,type,points,area(node),offset);++count;break;
        default:break;
        }
    },{}});
    return count;
}
}
