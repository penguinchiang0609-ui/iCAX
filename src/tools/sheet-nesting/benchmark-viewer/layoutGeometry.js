// Shared display bounds only: source contours and placement coordinates are never changed.
(() => {
    const placementPose=placement=>{
        const pose=placement.transform||placement.pose||placement;
        return {x:pose.x,y:pose.y,angle:pose.angle??pose.angleDegrees??0,mirrored:pose.mirrored===true};
    };
    function geometryBounds(entries){
        let minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity,known=true;
        const include=(x,y)=>{if(!Number.isFinite(x)||!Number.isFinite(y)){known=false;return;}minX=Math.min(minX,x);minY=Math.min(minY,y);maxX=Math.max(maxX,x);maxY=Math.max(maxY,y);};
        for(const {geometry,pose} of entries){
            if(!geometry||![pose.x,pose.y,pose.angle].every(Number.isFinite)){known=false;continue;}
            const radians=pose.angle*Math.PI/180,c=Math.cos(radians),s=Math.sin(radians);
            const world=point=>{const x=point[0]??point.x??point.X,sourceY=point[1]??point.y??point.Y,y=pose.mirrored?-sourceY:sourceY;return [pose.x+x*c-y*s,pose.y+x*s+y*c];};
            if(geometry.radius>0){const center=world(geometry.center||[0,0]);include(center[0]-geometry.radius,center[1]-geometry.radius);include(center[0]+geometry.radius,center[1]+geometry.radius);}
            else if(geometry.outer?.length||geometry.rings?.length){for(const ring of geometry.rings||[geometry.outer])for(const point of ring)include(...world(point));}
            else known=false;
        }
        return known&&Number.isFinite(minX)?{minX,minY,maxX,maxY}:null;
    }
    function occupiedBounds(layout,originals){
        return geometryBounds((layout.placements||[]).map(placement=>({geometry:originals.get(placement.part||placement.partId||placement.partID)?.geometry,pose:placementPose(placement)})));
    }
    function displayViewport(layout,bounds,mode,includeOrigin=false){
        const width=layout.width,height=layout.height,full={x:0,y:0,width,height,used:false};
        if(mode==="full"||!bounds)return full;
        const padding=Math.max(bounds.maxX-bounds.minX,bounds.maxY-bounds.minY,1)*.025;
        let x0=bounds.minX<0?bounds.minX-padding:Math.max(0,bounds.minX-padding);
        let y0=bounds.minY<0?bounds.minY-padding:Math.max(0,bounds.minY-padding);
        const x1=bounds.maxX>width?bounds.maxX+padding:Math.min(width,bounds.maxX+padding);
        const y1=bounds.maxY>height?bounds.maxY+padding:Math.min(height,bounds.maxY+padding);
        if(includeOrigin){x0=Math.min(0,x0);y0=Math.min(0,y0);}
        return {x:x0,y:y0,width:Math.max(x1-x0,1e-9),height:Math.max(y1-y0,1e-9),used:true};
    }
    globalThis.PlaneLayoutGeometry={placementPose,geometryBounds,occupiedBounds,displayViewport};
})();
