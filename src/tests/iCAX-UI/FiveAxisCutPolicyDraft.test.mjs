import assert from 'node:assert/strict';
import { cutPolicyDraft, processDraftDefaults } from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';
import { saveCommittedCutEdit } from '../../apps/five-axis-cam/webpage/cutEditPersistence.mjs';

const draft={...processDraftDefaults,cutPoseInitialization:'FixedDirection',cutAxisI:'1',cutAxisJ:'2',cutAxisK:'3',
  cutLeadTilt:'invalid hidden draft',cutSideTilt:'95'};
const before=structuredClone(draft),policy=cutPolicyDraft(draft);
assert.equal(policy.pose.leadAngleRad,0);assert.equal(policy.pose.sideAngleRad,0);
assert.ok(Math.abs(Math.hypot(...policy.pose.direction)-1)<1e-12);assert.deepEqual(draft,before);
assert.throws(()=>cutPolicyDraft({...draft,cutPoseInitialization:'SurfaceNormal'}),/前倾角/);
// A failed disk write is distinct from a rejected transaction: the editor can
// still refresh the acknowledged native state and retry only the file write.
const view={},logs=[];let writes=0;
const context={projectProxy:{state:{projectPath:'cut.i5cam'},async save(){if(++writes===1)throw Error('disk failure');}}};
assert.equal(await saveCommittedCutEdit(context,view,{appendProjectLog(_c,_level,text){logs.push(text)}}),false);
assert.match(view.fiveCutSaveError,/disk failure/);assert.equal(logs.length,1);
assert.equal(await saveCommittedCutEdit(context,view),true);assert.equal(view.fiveCutSaveError,'');
console.log('PASS fixed IJK ignores hidden tilt drafts; acknowledged edits survive failed file writes and save retry');
