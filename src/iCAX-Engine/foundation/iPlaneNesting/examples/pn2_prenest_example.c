#include "alma_study.h"
#include <stdio.h>

int main(void){
    PN2_Session session=PN2_CreateSession();
    const PN2_Point outline[]={{0,0},{4,0},{4,2},{0,2}};
    const PN2_Point pivot={0,0},position={8,3};
    PN2_Orientation pose=PN2_CreateOrientation(0);
    PN2_Shape shape=PN2_AddShape(session,outline,4,pivot);
    PN2_Part parts[3];
    PN2_Sheet sheet;
    PN2_Result manual,result;
    int count=0,index;
    double length=0;
    if(PN2_IsObjectError(session)||PN2_IsObjectError(shape)||
       PN2_AddSeveralParts(session,shape,&pose,1,3,parts))return 1;
    sheet=PN2_AddSheet(session,10,20);
    if(PN2_IsObjectError(sheet)||PN2_SetPreNestedGlueMode(session,parts[0],ALWAYS_GLUE))return 1;
    manual=PN2_PreNest(session,sheet,parts,&pose,&position,1);
    if(PN2_IsObjectError(manual))return 1;
    result=PN2_NestMore(session,manual,parts+1,2,.5);
    if(PN2_IsObjectError(result)||PN2_GetResultInfos(session,result,&length,&count)||count!=3)return 1;
    printf("PreNest + NestMore C client: %d parts, occupied length %.3f\n",count,length);
    for(index=0;index<count;++index){
        PN2_Part part;
        PN2_Point point;
        PN2_Orientation orientation;
        if(PN2_GetKthNestedPart(session,result,index,&part,&point,&orientation))return 1;
        if(part==parts[0]&&(point.x!=position.x||point.y!=position.y))return 1;
        printf("  part %d: %.3f %.3f, angle %.3f\n",index,point.x,point.y,orientation.min_angle);
    }
    return PN2_DeleteSession(session)?0:1;
}
