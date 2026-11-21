struct PlateStationError:std::runtime_error {
    size_t StationIndex;
    PlateStationError(size_t station,const char* reason):std::runtime_error(reason),StationIndex(station){}
};
void ValidateJoints(const SectionPlateInput& in){
    if(in.JointMode!="Weld"&&in.JointMode!="TabSlot")throw std::invalid_argument("PLATE_JOINT_MODE_INVALID");
    if(in.JointMode=="Weld"){
        if(in.Automatic&&in.Pattern=="Skeleton"&&(!std::isfinite(in.JointClearance)||!std::isfinite(in.JointWeb)||in.JointClearance<0||in.JointClearance>in.Thickness/2||in.JointWeb<.5||in.JointWeb>50))throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");
        return;
    }
    for(double n:{in.JointClearance,in.TenonWidth,in.TenonPitch,in.JointWeb})if(!std::isfinite(n))throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");
    if(in.JointClearance<0||in.JointClearance>std::min(in.Thickness,in.BaseThickness)/2||in.TenonWidth<in.Thickness||in.TenonWidth>500||in.JointWeb<.5||in.JointWeb>50||in.TenonPitch<in.TenonWidth+2*in.JointWeb+in.JointClearance||in.TenonPitch>1000)throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");
    if(!std::isfinite(in.TenonRadius)||in.TenonRadius<0||in.TenonRadius>std::min((in.TenonWidth-1e-5)/2,in.BaseThickness/2))throw std::invalid_argument("PLATE_TENON_RADIUS_INVALID");
}
void ValidateWindows(const SectionPlateInput& in){
    if(in.WindowMode!="None"&&in.WindowMode!="Automatic")throw std::invalid_argument("PLATE_WINDOW_MODE_INVALID");
    if(in.WindowMode=="Automatic"&&(!std::isfinite(in.WindowWeb)||in.WindowWeb<in.Thickness||in.WindowWeb>500))throw std::invalid_argument("PLATE_WINDOW_WEB_INVALID");
}
void ValidateKeepOuts(const SectionPlateInput& in){
    if(in.KeepOuts.size()>250000)throw std::invalid_argument("PLATE_KEEPOUT_LIMIT");
    for(const auto& cell:in.KeepOuts){
        if(cell.Vertices.empty()||cell.Vertices.size()>128||!std::isfinite(cell.Margin)||cell.Margin<0||cell.Margin>1e6)throw std::invalid_argument("PLATE_KEEPOUT_INVALID");
        for(auto point:cell.Vertices)for(double value:point)if(!std::isfinite(value)||std::abs(value)>1e9)throw std::invalid_argument("PLATE_KEEPOUT_INVALID");
    }
}
