import {machiningTaskTimeoutMs} from './operationTiming.mjs';
import {queryTaskReceipt} from './taskProgressStream.mjs';

export const fixtureTaskRequestOptions=Object.freeze({timeoutMs:machiningTaskTimeoutMs});

export async function queryFixtureTask(context,task){
  return queryTaskReceipt(context,task,{method:'CamFixture.QueryTask',timeoutMs:machiningTaskTimeoutMs});
}
