/* Public C17 client for the shipped PN2 C ABI. */
#include "alma_study.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks=0;
#define CHECK(expression) do { ++checks; if(!(expression)){fprintf(stderr,"C material API check failed at %d\n",__LINE__);return 1;} } while(0)
int main(void){
    PN2_Session session=PN2_CreateSession();
    PN2_Point outer[5]={{0,0},{40,0},{40,30},{0,30},{0,0}};
    PN2_Point defect[4]={{15,10},{25,10},{25,20},{15,20}};
    PN2_Point piece[4]={{0,0},{5,0},{5,4},{0,4}};
    PN2_Sheet sheets[4];PN2_Contour contour,hole;PN2_Shape shape;
    PN2_Part parts[4];PN2_Orientation pose=PN2_CreateOrientation(0);
    int i,placed=0;double length=0;
    CHECK(!PN2_IsObjectError(session));
    CHECK(sizeof(PN2_Point)==16&&sizeof(PN2_Orientation)==24);
    sheets[0]=PN2_AddPolygonalSheet(session,outer,5);
    sheets[1]=PN2_AddPolygonalSheetWithBorderGap(session,outer,4,1);
    contour=PN2_CreateContourFromPoints(session,outer,4);
    hole=PN2_CreateContourFromPoints(session,defect,4);
    CHECK(!PN2_IsObjectError(contour)&&!PN2_IsObjectError(hole));
    sheets[2]=PN2_AddSheetFromContour(session,contour);
    sheets[3]=PN2_AddSheetFromContourWithBorderGap(session,contour,1);
    for(i=0;i<4;++i)CHECK(!PN2_IsObjectError(sheets[i]));
    CHECK(PN2_SheetAddDefect(session,sheets[0],defect,4)==0);
    CHECK(PN2_SheetAddDefectWithOffset(session,sheets[1],defect,4,1)==0);
    CHECK(PN2_SheetAddDefectFromContour(session,sheets[2],hole)==0);
    CHECK(PN2_SheetAddDefectFromContourWithOffset(session,sheets[3],hole,1)==0);
    shape=PN2_AddShape(session,piece,4,(PN2_Point){0,0});
    CHECK(!PN2_IsObjectError(shape));
    CHECK(PN2_AddSeveralParts(session,shape,&pose,1,4,parts)==0);
    for(i=0;i<4;++i){
        PN2_Result result=PN2_Nest(session,sheets[i],parts,4,.8);
        int count=0,k;double occupied=-1;
        CHECK(!PN2_IsObjectError(result));
        CHECK(PN2_GetResultInfos(session,result,&occupied,&count)==0&&count==4);
        for(k=0;k<count;++k){
            PN2_Part actual=0;PN2_Point p;PN2_Orientation actualPose;int found=0,n;
            double margin=i%2?1:0;
            CHECK(PN2_GetKthNestedPart(session,result,k,&actual,&p,&actualPose)==0);
            for(n=0;n<4;++n)if(actual==parts[n])++found;
            CHECK(found==1&&actualPose.min_angle==0&&actualPose.max_angle==0&&!actualPose.horizontal_flip);
            CHECK(p.x>=margin-1e-6&&p.y>=margin-1e-6&&p.x+5<=40-margin+1e-6&&p.y+4<=30-margin+1e-6);
            CHECK(p.x+5<=15-margin+1e-6||p.x>=25+margin-1e-6||p.y+4<=10-margin+1e-6||p.y>=20+margin-1e-6);
            ++placed;
        }
        length+=occupied;
    }
    CHECK(PN2_DeleteSession(session)>0);
    printf("{\"pass\":true,\"language\":\"C\",\"standardSheetEntries\":8,\"cases\":4,\"placements\":%d,\"checks\":%d,\"occupiedLengthSum\":%.9g}\n",placed,checks,length);
    return 0;
}
