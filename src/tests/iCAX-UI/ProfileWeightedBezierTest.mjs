import assert from "node:assert/strict";
import { profileSvgGeometry } from "../../apps/tube-designer/webpage/profileSvg.mjs";

const controlPoints = [[10,0],[10,10],[0,10]];
const weights = [1,Math.SQRT1_2,1];
const render = segment => profileSvgGeometry({contours:[{kind:"path",segments:[
  segment,{kind:"line",start:[0,10],end:[10,0]},
]}]});
const rational = render({kind:"bezier",controlPoints,weights});
const nurbs = render({kind:"nurbs",controlPoints,weights,degree:2,knots:[0,1],multiplicities:[3,3]});
assert.deepEqual(rational,nurbs);
assert.notEqual(rational.markup,render({kind:"bezier",controlPoints}).markup);
assert.ok(rational.bounds);
assert.deepEqual(weights,[1,Math.SQRT1_2,1]);
const trimmed = profileSvgGeometry({contours:[{kind:"path",segments:[{
  kind:"bspline",degree:1,periodic:true,controlPoints:[[0,0],[10,0],[10,10],[0,10]],
  knots:[0,1,2,3,4],startParameter:0,endParameter:0.5,
}]}]});
assert.equal(trimmed.bounds.maxX,5,"A trimmed periodic curve must end at its trim, not close at its start");
console.log("Weighted Bezier preview preserves rational geometry.");
