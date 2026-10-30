import test from 'node:test';
import assert from 'node:assert/strict';
import {uiOutlineGeometry} from '../../web/ui-system.js';

// Interpret the generated orthogonal/diagonal outline, including implicit
// relative positions of H/V. A 3px leading stroke must stay in the hit box.
function vertices(path) {
  const result=[];let x=0,y=0;
  for(const [,command,argumentsText] of path.matchAll(/([MLHVZ])([^MLHVZ]*)/g)) {
    const values=argumentsText.trim().split(/[\s,]+/).filter(Boolean).map(Number);
    if(command==='M'||command==='L') { [x,y]=values;result.push([x,y]); }
    if(command==='H') { x=values[0];result.push([x,y]); }
    if(command==='V') { y=values[0];result.push([x,y]); }
  }
  return result;
}

test('action, choice and icon perimeters contain their strongest stroke at narrow and fractional sizes',()=>{
  for(const [width,height] of [[4,4],[24,24],[32,32],[44,44],[123.38,45.47],[640,110]]) {
    for(const kind of ['action','choice','icon','notice'])for(const origin of ['top','bottom','corner','left']) {
      const geometry=uiOutlineGeometry(width,height,{kind,origin});
      assert.ok(geometry);
      assert.equal(geometry.outline,geometry.idle,'Idle and interactive outlines share one perimeter.');
      assert.ok(geometry.fraction>0&&geometry.fraction<=1);
      for(const [x,y] of vertices(geometry.outline)) {
        assert.ok(x>=1.5&&x<=width-1.5,`${kind}/${origin}: x=${x} escaped ${width}`);
        assert.ok(y>=1.5&&y<=height-1.5,`${kind}/${origin}: y=${y} escaped ${height}`);
      }
    }
  }
});

test('trace origins rotate the same closed shape instead of introducing another border',()=>{
  const outlines=['top','bottom','corner','left'].map(origin=>uiOutlineGeometry(120,44,{origin}).outline);
  assert.equal(new Set(outlines.map(d=>d.split('L')[0])).size,4,'Each reveal starts at a different physical edge.');
  const footprint=d=>vertices(d).map(point=>point.join(',')).sort();
  assert.deepEqual(footprint(outlines[0]),footprint(outlines[1]));
  assert.deepEqual(footprint(outlines[1]),footprint(outlines[2]));
  assert.deepEqual(footprint(outlines[2]),footprint(outlines[3]));
  assert.ok(outlines.every(d=>d.endsWith('Z')));
  const leading=vertices(outlines[3]);
  assert.equal(leading[0][0],leading[1][0],'Left origin begins with the vertical leading edge.');
});

test('compact notice geometry preserves fixed corner cuts and centered inward notches as width grows',()=>{
  for(const width of [280,640,1600]) {
    const points=vertices(uiOutlineGeometry(width,48,{kind:'notice'}).outline);
    const top=points.filter(([,y])=>y<10), bottom=points.filter(([,y])=>y>38);
    assert.ok(top.some(([x,y])=>x===9.5&&y===1.5),'Corner cut remains eight pixels.');
    assert.ok(top.some(([x,y])=>x===width*.3+4&&y===5.5));
    assert.ok(bottom.some(([x,y])=>x===width*.3+4&&y===42.5));
    for(const [x,y] of points.filter(([,y])=>y!==24))assert.ok(points.some(([mirrorX,mirrorY])=>Math.abs(mirrorX-(width-x))<.02&&Math.abs(mirrorY-(48-y))<.02),'Opposite rail geometry stays balanced.');
  }
});

test('pattern has distinct inset rails and invalid sizes never emit corrupt SVG coordinates',()=>{
  const geometry=uiOutlineGeometry(180,80,{kind:'pattern'});
  assert.notEqual(geometry.outline,geometry.idle);
  assert.ok(!geometry.outline.includes('NaN'));
  for(const size of [0,-1,NaN,Infinity,3])assert.equal(uiOutlineGeometry(size,44),null);
  assert.throws(()=>uiOutlineGeometry(44,44,{origin:'outside'}),RangeError);
});
