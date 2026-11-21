// Include in api.cpp's extern "C" block after api_material.inl helpers.
PN2_Sheet PN2S_CALL PN2_AddPolygonalSheet(PN2_Session h,const PN2_Point *points,int count) {
    return invoke<PN2_Sheet>([&]{auto &s=session(h);return handle<PN2_Sheet>(apiMaterialAddSheet(s,apiMaterialRing(points,count),0));});
}
PN2_Sheet PN2S_CALL PN2_AddPolygonalSheetWithBorderGap(PN2_Session h,const PN2_Point *points,int count,double gap) {
    return invoke<PN2_Sheet>([&]{auto &s=session(h);return handle<PN2_Sheet>(apiMaterialAddSheet(s,apiMaterialRing(points,count),gap));});
}
PN2_Sheet PN2S_CALL PN2_AddSheetFromContour(PN2_Session h,PN2_Contour contour) {
    return invoke<PN2_Sheet>([&]{auto &s=session(h);return handle<PN2_Sheet>(apiMaterialContourSheet(s,contour,0));});
}
PN2_Sheet PN2S_CALL PN2_AddSheetFromContourWithBorderGap(PN2_Session h,PN2_Contour contour,double gap) {
    return invoke<PN2_Sheet>([&]{auto &s=session(h);return handle<PN2_Sheet>(apiMaterialContourSheet(s,contour,gap));});
}
PN2_ErrorCode PN2S_CALL PN2_SheetAddDefect(PN2_Session h,PN2_Sheet sheet,const PN2_Point *points,int count) {
    return invoke<int>([&]{auto &s=session(h);apiMaterialAddDefect(s,id(sheet),apiMaterialRing(points,count),0);return 0;});
}
PN2_ErrorCode PN2S_CALL PN2_SheetAddDefectWithOffset(PN2_Session h,PN2_Sheet sheet,const PN2_Point *points,int count,double offset) {
    return invoke<int>([&]{auto &s=session(h);apiMaterialAddDefect(s,id(sheet),apiMaterialRing(points,count),offset);return 0;});
}
PN2_ErrorCode PN2S_CALL PN2_SheetAddDefectFromContour(PN2_Session h,PN2_Sheet sheet,PN2_Contour contour) {
    return invoke<int>([&]{auto &s=session(h);apiMaterialAddDefect(s,id(sheet),contourPolygon(lookup(s.contours,id(contour)),true),0);return 0;});
}
PN2_ErrorCode PN2S_CALL PN2_SheetAddDefectFromContourWithOffset(PN2_Session h,PN2_Sheet sheet,PN2_Contour contour,double offset) {
    return invoke<int>([&]{auto &s=session(h);apiMaterialAddDefect(s,id(sheet),contourPolygon(lookup(s.contours,id(contour)),true),offset);return 0;});
}
PN2_ErrorCode PN2S_CALL PN2S_SetMaterialGap(PN2_Session h,double gap) {
    return invoke<int>([&]{auto &s=session(h);require(std::isfinite(gap)&&gap>=0&&gap<=1000,PN2S_INVALID,"Material gap must be finite and in [0,1000]");
        s.gap=gap;s.materialGap=true;return 0;});
}
PN2_ErrorCode PN2S_CALL PN2S_AddMaterialParts(PN2_Session h,PN2_Shape shape,const PN2_Orientation *poses,int poseCount,int quantity,PN2_Part *output) {
    return invoke<int>([&]{auto &s=session(h);lookup(s.shapes,id(shape));const auto orientations=readPoses(poses,poseCount,64);
        require(output&&quantity>0&&quantity<=10000&&s.parts.size()+size_t(quantity)<=10000,PN2S_INVALID,"Invalid material part quantity/output");
        std::vector<Id> created;created.reserve(size_t(quantity));
        try{for(int index=0;index<quantity;++index){Part part{id(shape),orientations};part.materialRoute=true;const auto key=token();s.parts.emplace(key,std::move(part));created.push_back(key);}}
        catch(...){for(const auto key:created)s.parts.erase(key);throw;}
        for(int index=0;index<quantity;++index)output[index]=handle<PN2_Part>(created[size_t(index)]);return 0;});
}
PN2_ErrorCode PN2S_CALL PN2S_GetMaterialResultInfos(PN2_Session h,const void *object,PN2S_MaterialResultInfo *output) {
    return invoke<int>([&]{auto &s=session(h);require(output&&output->struct_size>=sizeof(PN2S_MaterialResultInfo),PN2S_INVALID,"Invalid material result information output");
        std::shared_ptr<const MaterialResultMetadata> metadata;const auto key=reinterpret_cast<Id>(object);
        if(const auto found=s.results.find(key);found!=s.results.end())metadata=found->second.materialInfo;
        else if(const auto multi=s.multis.find(key);multi!=s.multis.end())metadata=multi->second.materialInfo;
        else throw Failure(PN2S_INVALID,"Invalid or cross-session result/multi handle");
        PN2S_MaterialResultInfo info{};info.struct_size=sizeof(info);info.is_material=metadata?1:0;info.stopped=metadata&&metadata->stopped?1:0;
        info.trials=metadata?metadata->trials:0;info.warning_utf8=metadata?metadata->warning.c_str():"";*output=info;return 0;});
}
