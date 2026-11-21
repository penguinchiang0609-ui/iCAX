#include "alma_study.h"
#include <stdio.h>
int main(void) {
    PN2_Session session=PN2_CreateSession();
    PN2_Point polygon[]={{0,0},{3,0},{3,2},{0,2}},pivot={0,0};
    PN2_Orientation poses[2];PN2_Part parts[6];double values[6]={6,6,6,6,6,6};
    PN2S_Direction18Options options={sizeof(PN2S_Direction18Options),2,0,0,8,20,36,0,0,10};
    /* Explicit fields. Generator18 alone stops when remaining < 2 * batch. */
    PN2_Shape shape=PN2_AddShape(session,polygon,4,pivot);
    PN2_Sheet sheet=PN2_AddSheet(session,8,20);
    PN2_Result result;double length=0;int count=0,k,unplaced=0;
    poses[0]=PN2_CreateOrientation(0);poses[1]=PN2_CreateOrientation(90);
    if(PN2_IsObjectError(session)||PN2_IsObjectError(shape)||PN2_IsObjectError(sheet)||PN2S_SetPitch(session,.5)||
       PN2S_SetRandomSeed(session,1)||PN2_AddSeveralParts(session,shape,poses,2,6,parts))return 1;
    result=PN2S_NestDirection18(session,sheet,parts,6,values,&options,10);
    if(PN2_IsObjectError(result)){fprintf(stderr,"%s\n",PN2S_LastErrorDetail());PN2_DeleteSession(session);return 1;}
    if(PN2_GetResultInfos(session,result,&length,&count)||count!=4){PN2_DeleteSession(session);return 1;}
    for(k=0;k<6;++k) {
        PN2_Point pos;PN2_Orientation pose;int found=PN2_GetNestedPart(session,result,parts[k],&pos,&pose);
        if(found<0){PN2_DeleteSession(session);return 1;}unplaced+=found==0;
    }
    if(unplaced!=2){PN2_DeleteSession(session);return 1;}
    printf("{\"route\":\"Direction18 research API\",\"required\":6,\"placed\":%d,\"unplaced\":%d,\"length\":%.17g,\"placements\":[",count,unplaced,length);
    for(k=0;k<count;++k){
        PN2_Part part;PN2_Point pos;PN2_Orientation pose;
        if(PN2_GetKthNestedPart(session,result,k,&part,&pos,&pose)){PN2_DeleteSession(session);return 1;}
        printf("%s{\"x\":%.17g,\"y\":%.17g,\"angle\":%.17g}",k?",":"",pos.x,pos.y,pose.min_angle);
    }
    printf("]}\n");PN2_DeleteSession(session);return 0;
}
