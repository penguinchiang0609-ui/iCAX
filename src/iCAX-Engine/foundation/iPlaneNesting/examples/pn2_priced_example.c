#include "alma_study.h"
#include <stdio.h>
int main(void) {
    PN2_Session session=PN2_CreateSession();
    const PN2_Point points[]={{0,0},{4,0},{4,4},{0,4}},pivot={0,0};
    PN2_Orientation pose=PN2_CreateOrientation(0);
    PN2_Part parts[2];int prices[]={10,100};
    int i,count=0;double length=0;
    if(PN2_IsObjectError(session))return 1;
    /* Separate source shapes keep their separate normalized prices. */
    for(i=0;i<2;++i){PN2_Shape shape=PN2_AddShape(session,points,4,pivot);
        if(PN2_IsObjectError(shape))return 2;
        parts[i]=PN2_AddPart(session,shape,&pose,1);
        if(PN2_IsObjectError(parts[i]))return 3;
    }
    {
        PN2_Sheet sheet=PN2_AddSheet(session,4,5);
        PN2_Result result=PN2_PricedNest(session,sheet,parts,prices,2,.2);
        PN2_Part selected;PN2_Point position;PN2_Orientation actual;
        if(PN2_IsObjectError(result)||PN2_GetResultInfos(session,result,&length,&count)||count!=1)return 4;
        if(PN2_GetKthNestedPart(session,result,0,&selected,&position,&actual)||selected!=parts[1])return 5;
        printf("Priced C client: %d part, price %d, position %.3f %.3f, length %.3f\n",
            count,prices[1],position.x,position.y,length);
    }
    PN2_DeleteSession(session);return 0;
}
