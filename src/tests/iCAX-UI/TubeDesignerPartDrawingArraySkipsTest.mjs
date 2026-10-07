import assert from "node:assert/strict";
import {
  createDrawingState, normalizeDrawingFeature, installDrawingCatalogue,
  drawingArrayDimension, setDrawingArrayDimension, drawingArraySkipText,
  resolveDrawingArraySkips, validateDrawingFeature, validateDrawing,
  updateDrawingField, addDrawingFeature, editDrawingFeature, getDrawingPayload,
} from "../../apps/tube-designer/webpage/partDrawingModel.mjs";

const tool={id:"circle",version:"test-1",digest:"drawing-skips",target:"side",parameters:[],defaultParameters:{}};
const feature=(values={})=>normalizeDrawingFeature({type:"circle",recordKind:"tool",station:100,
  toolRef:{id:tool.id,version:tool.version,digest:tool.digest},arrayCount:4,rowCount:3,arrayPitch:20,rowPitch:10,...values});
const model=features=>{
  const state=createDrawingState({entityId:"drawing-skips",length:1000});
  installDrawingCatalogue(state,{tools:[tool]});state.features=features;return state;
};
const resolved=(text,values={})=>resolveDrawingArraySkips(feature({skipInstancesText:text,...values}));
const invalid=(text,pattern,values={})=>assert.throws(()=>resolved(text,values),pattern,text);

// The table reads first-dimension X : second-dimension row. The unchanged native
// regular-array recipe stores zero-based row:column instead.
assert.deepEqual(resolved("2:1,3:2"),["0:1","1:2"]);
assert.deepEqual(resolved(" 2：1； 3:2\n1:3 "),["0:1","1:2","2:0"]);
assert.deepEqual(resolved("3",{rowCount:1}),["0:2"]);
assert.deepEqual(resolved("4:*"),["0:3","1:3","2:3"]);
assert.deepEqual(resolved("*:2"),["1:0","1:1","1:2","1:3"]);
assert.deepEqual(resolved("4:*,*:2"),["0:3","1:0","1:1","1:2","1:3","2:3"],
  "wildcard selectors can overlap without duplicating native instances");
assert.deepEqual(resolved(""),[]);
for(const text of ["0:1","1:0","5:1","1:4","5:*","*:4","9007199254740992:1"])
  invalid(text,/范围|正整数/,{});
for(const text of ["-1:1","1.5:1","1","1:1:1","2:x","globalThis.drawingSkipPwned=true"])
  invalid(text,/填写|序号|无效/);
for(const text of ["2:1,2:1","2:1,02:1","*:2,*:2"])
  invalid(text,/重复/);
for(const text of ["*:*","1:* 2:* 3:* 4:*","*:1,*:2,*:3"])
  invalid(text,/所有|全部/);
for(const text of ["0","5","9007199254740992"])
  invalid(text,/范围|正整数/,{rowCount:1});
invalid("1,1",/重复/,{rowCount:1});
invalid("1 2 3 4",/所有|全部/,{rowCount:1});
invalid("*",/所有|全部/,{rowCount:1});
assert.equal(globalThis.drawingSkipPwned,undefined);

// Native recipes without an editor draft display the same human index order;
// explicit current text wins over a previous resolved list.
assert.equal(drawingArraySkipText(feature({skippedInstances:["0:1","1:2"]})),"2:1, 3:2");
assert.equal(drawingArraySkipText(feature({rowCount:1,skippedInstances:["0:2"]})),"3");
assert.equal(drawingArraySkipText(feature({skipInstancesText:"4:*",skippedInstances:["0:1"]})),"4:*");
const singleRowTwo=feature({rowCount:1,skipInstancesText:"2:1"});
assert.equal(drawingArrayDimension(singleRowTwo),"two","a saved two-dimensional selector remains editable with one transverse row");
assert.deepEqual(resolveDrawingArraySkips(singleRowTwo),["0:1"]);

// Each dimension remembers its own text without letting dormant selectors
// invalidate a smaller active matrix or remove its only seed tool.
const dormant=feature({skipInstancesText:"2:1,3:2"});
setDrawingArrayDimension(dormant,"none");
assert.equal(drawingArraySkipText(dormant),"");assert.deepEqual(resolveDrawingArraySkips(dormant),[]);
assert.equal(validateDrawingFeature(dormant),"");
setDrawingArrayDimension(dormant,"one");
assert.equal(drawingArraySkipText(dormant),"");assert.deepEqual(resolveDrawingArraySkips(dormant),[]);
const dormantState=model([dormant]);dormantState.draft=dormant;
assert.equal(updateDrawingField(dormantState,{value:"3",dataset:{tubeDesignerPunchField:"skipInstancesText"}}),true);
assert.equal(drawingArraySkipText(dormantState.draft),"3");
assert.deepEqual(resolveDrawingArraySkips(dormantState.draft),["0:2"]);
setDrawingArrayDimension(dormantState.draft,"two");
assert.equal(drawingArraySkipText(dormantState.draft),"2:1,3:2");
assert.deepEqual(resolveDrawingArraySkips(dormantState.draft),["0:1","1:2"]);
setDrawingArrayDimension(dormantState.draft,"one");
assert.equal(drawingArraySkipText(dormantState.draft),"3");
setDrawingArrayDimension(dormantState.draft,"none");
const inactivePayload=getDrawingPayload(dormantState).features[0];
assert.deepEqual(inactivePayload.skippedInstances,[]);
assert.equal(inactivePayload.skipInstancesText,"");
assert.equal(inactivePayload.arrayEditing,undefined);

// Transport, edit transactions and saved JSON preserve resolved row:column
// indices and current text, while only editor-local dormant values stay behind.
const editing=model([]);editing.draft=feature({skipInstancesText:"2:1,3:2"});
assert.equal(addDrawingFeature(editing),true);
const original=getDrawingPayload(editing).features[0];
assert.equal(original.skipInstancesText,"2:1,3:2");assert.deepEqual(original.skippedInstances,["0:1","1:2"]);
assert.equal(editDrawingFeature(editing,"edit",0),true);
updateDrawingField(editing,{value:"4:*",dataset:{tubeDesignerPunchField:"skipInstancesText"}});
assert.equal(addDrawingFeature(editing),true);
assert.deepEqual(getDrawingPayload(editing).features[0].skippedInstances,["0:3","1:3","2:3"]);
assert.equal(editDrawingFeature(editing,"undo"),true);
assert.deepEqual(getDrawingPayload(editing).features[0].skippedInstances,original.skippedInstances);
assert.equal(editDrawingFeature(editing,"redo"),true);
const saved=getDrawingPayload(editing);
assert.deepEqual(saved.features[0].skippedInstances,["0:3","1:3","2:3"]);
assert.equal(saved.features[0].arrayEditing,undefined);
const reopened=createDrawingState({entityId:"reopened",length:1000,properties:{"tubeDesigner.partDrawing":JSON.parse(JSON.stringify(saved))}});
installDrawingCatalogue(reopened,{tools:[tool]});
assert.equal(drawingArraySkipText(reopened.features[0]),"4:*");
assert.deepEqual(getDrawingPayload(reopened).features[0].skippedInstances,saved.features[0].skippedInstances);

// Skipping only changes retained instances. Safety limits always count every
// candidate, including opposite copies and the total across different features.
assert.equal(validateDrawingFeature(feature({arrayCount:1000,rowCount:1,skipInstancesText:"1"})),"");
assert.match(validateDrawingFeature(feature({arrayCount:1001,rowCount:1,skipInstancesText:"1"})),/1000/);
assert.match(validateDrawingFeature(feature({arrayCount:501,rowCount:1,opposite:true,skipInstancesText:"1,2"})),/1000/);
assert.match(validateDrawingFeature(feature({arrayCount:100,rowCount:11,skipInstancesText:"*:1"})),/1000/);
assert.match(validateDrawing(model([
  feature({arrayCount:600,rowCount:1,skipInstancesText:"1"}),
  feature({arrayCount:401,rowCount:1,skipInstancesText:"1"}),
])),/1000/);
const disabled=feature({enabled:false,arrayCount:Infinity,rowCount:Infinity,skipInstancesText:"bad"});
assert.equal(validateDrawingFeature(disabled),"");
assert.doesNotThrow(()=>getDrawingPayload(model([disabled])),"a disabled invalid array never allocates an unbounded transport");

console.log("Part drawing regular skips: X:row syntax, wildcard/duplicate/bounds validation, dormant dimensional drafts, transport/history/save and candidate limits passed.");
