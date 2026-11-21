#include "alma_study.h"
#include <stdio.h>
int main(void) {
    PN2_Session session=PN2_CreateSession();
    PN2S_StrategyEntry entries[]={{8,250,2,0},{3,17,1,0}};
    PN2S_ScheduleOptions options={sizeof(PN2S_ScheduleOptions),42,2,0,12,1,30,3,2.5};
    PN2S_StrategySchedule schedule=PN2S_CreateStrategySchedule(session,entries,2,&options);
    PN2S_StrategyResult pool[]={{100,3},{100,8},{100,3},{100,0},{100,3},{100,8},{100,3},{100.1,8}};
    PN2S_StrategyWeight weights[2];int count,prefix,selected,index=0,id,trials,limit,status;
    if(PN2_IsObjectError(schedule)){fprintf(stderr,"%s\n",PN2S_LastErrorDetail());PN2_DeleteSession(session);return 1;}
    printf("{\"scope\":\"recovered scheduler only; caller supplies managers and ordered candidate pool\",\"budgets\":[");
    for(index=0;index<2;++index){PN2S_StrategyEntry entry;double weight;if(PN2S_GetScheduleEntry(session,schedule,index,&entry,&weight))return 1;
        printf("%s{\"id\":%d,\"budget\":%.17g,\"samplingWeight\":%.17g}",index?",":"",entry.strategy_id,entry.accumulated_budget,weight);}
    printf("],\"selectedIds\":[");index=0;
    while((status=PN2S_NextScheduledStrategy(session,schedule,&id,&trials,&limit))==1){printf("%s%d",index?",":"",id);++index;}
    if(status<0 || index!=12)return 1;
    if(PN2S_SelectStrategyWeights(pool,8,trials,2,2,weights,2,&count,&prefix,&selected) || count!=2 || prefix!=7 || selected!=7)return 1;
    printf("],\"trials\":%d,\"poolLimit\":%d,\"newWeights\":[",trials,limit);
    for(index=0;index<count;++index)printf("%s{\"id\":%d,\"weight\":%.17g}",index?",":"",weights[index].strategy_id,weights[index].weight);
    printf("]}\n");PN2S_DeleteStrategySchedule(session,schedule);PN2_DeleteSession(session);return 0;
}
