// Screen-space intent filtering, independent of camera projection and frame rate.
// A short travel window tolerates jitter and direction reversal, yet lets a
// deliberate turn leave an axis without catching up previously ignored motion.
export class OrbitAxisSnap {
  constructor() {
    this.samples=[];this.distance=0;this.axis=null;
    this.started=false;this.pendingX=0;this.pendingY=0;
  }

  move(dx,dy) {
    const length=Math.hypot(dx,dy);
    if(!Number.isFinite(length)||!length)return {dx:0,dy:0,axis:this.axis};
    this.samples.push({x:Math.abs(dx),y:Math.abs(dy),length});this.distance+=length;
    while(this.distance>24){
      const first=this.samples[0],trim=Math.min(first.length,this.distance-24),fraction=trim/first.length;
      first.x*=1-fraction;first.y*=1-fraction;first.length-=trim;this.distance-=trim;
      if(first.length<=0)this.samples.shift();
    }
    if(!this.started){
      this.pendingX+=dx;this.pendingY+=dy;
      if(this.distance<4)return {dx:0,dy:0,axis:null};
      this.started=true;dx=this.pendingX;dy=this.pendingY;
    }
    const x=this.samples.reduce((n,s)=>n+s.x,0),y=this.samples.reduce((n,s)=>n+s.y,0);
    if(this.axis==='horizontal'&&x<1.5*y)this.axis=null;
    if(this.axis==='vertical'&&y<1.5*x)this.axis=null;
    if(!this.axis){
      if(x>=3*y)this.axis='horizontal';
      else if(y>=3*x)this.axis='vertical';
    }
    return {dx:this.axis==='vertical'?0:dx,dy:this.axis==='horizontal'?0:dy,axis:this.axis};
  }
}
