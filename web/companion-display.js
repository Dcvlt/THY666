/* SPDX-License-Identifier: GPL-3.0-or-later
 * Frame-first enhancement of the shared C display. No menu/state recreation.
 * Only exact, isolated canonical glyphs become type; every other pixel remains.
 */
export const DISPLAY_WIDTH=128,DISPLAY_HEIGHT=64,DISPLAY_BYTES=1024;
/* Independent original glyphs from core/thy_ui.c, checked against that source
 * by the regression suite. Lowercase text is already normalized by C, except b. */
export const CANONICAL_GLYPHS=Object.freeze({
  '0':0x1f11111f,'1':0x001f1200,'2':0x1715151d,'3':0x1f151511,'4':0x1f040407,
  '5':0x1d151517,'6':0x1d15151f,'7':0x03051901,'8':0x1f15151f,'9':0x1f151517,
  A:0x1e05051e,B:0x0a15151f,b:0x0814141f,C:0x1111110e,D:0x0e11111f,E:0x1115151f,
  F:0x0105051f,G:0x1d15110e,H:0x1f04041f,I:0x001f0000,J:0x0f101008,K:0x110a041f,
  L:0x1010101f,M:0x1f02021f,N:0x1f08041f,O:0x0e11110e,P:0x0205051f,Q:0x1e19110e,
  R:0x120d051f,S:0x09151512,T:0x01011f01,U:0x0f10100f,V:0x030c1807,W:0x1f08081f,
  X:0x110a041b,Y:0x03041803,Z:0x13151911,'-':0x04040404,':':0x000a0000,
  '/':0x01020418,'#':0x0a1f0a1f,'+':0x040e0400,'>':0x00040a11,'<':0x110a0400,
  '?':0x02051501,'%':0x00130419,'(':0x00110e00,')':0x000e1100,'.':0x00100000,' ':0,
});
const glyphByBits=new Map();
for(const [character,bits] of Object.entries(CANONICAL_GLYPHS)){
  const key=bits>>>0;glyphByBits.set(key,glyphByBits.has(key)?null:character);
}
const validFrame=frame=>frame instanceof Uint8Array&&frame.length===DISPLAY_BYTES;
const pixel=(frame,x,y)=>x>=0&&x<128&&y>=0&&y<64?Boolean(frame[(y>>>3)*128+x]&(1<<(y&7))):false;
const setPixel=(frame,x,y,value)=>{
  if(x<0||x>=128||y<0||y>=64)return;
  const at=(y>>>3)*128+x,bit=1<<(y&7);if(value)frame[at]|=bit;else frame[at]&=~bit;
};
const backgroundLine=(frame,x,y,width,height,value)=>{
  for(let dy=0;dy<height;dy++)for(let dx=0;dx<width;dx++)
    if(pixel(frame,x+dx,y+dy)!==value)return false;
  return true;
};

function readCell(frame,claimed,x,y,scale,inverse,dim){
  if(x+4*scale>128||y+5*scale>64)return null;
  /* A complete blank/inverse row above and below prevents graph/box strokes
   * that merely resemble letters from becoming inferred text. */
  if(!backgroundLine(frame,x,y-scale,4*scale,scale,inverse)||
     !backgroundLine(frame,x,y+5*scale,4*scale,scale,inverse)||
     !backgroundLine(frame,x+4*scale,y,scale,5*scale,inverse))return null;
  let bits=0;
  for(let col=0;col<4;col++)for(let row=0;row<5;row++){
    let background=true,ink=true;
    for(let dy=0;dy<scale;dy++)for(let dx=0;dx<scale;dx++){
      const px=x+col*scale+dx,py=y+row*scale+dy;
      if(claimed[py*128+px])return null;
      const value=pixel(frame,px,py),expected=dim?!((px+py)&1):!inverse;
      if(value!==inverse)background=false;if(value!==expected)ink=false;
    }
    if(!background&&!ink)return null;
    if(!background)bits|=1<<(col*8+row);
  }
  const character=glyphByBits.get(bits>>>0);
  return character===undefined||character===null?null:character;
}

function liftRuns(frame,residual,claimed,scale,inverse,dim,texts){
  const step=5*scale;
  for(let y=scale;y+6*scale<=64;y++)for(let x=0;x+4*scale<=128;x++){
    const first=readCell(frame,claimed,x,y,scale,inverse,dim);
    if(first===null||first===' '||!backgroundLine(frame,x-scale,y,scale,5*scale,inverse))continue;
    let text='',nonblank=0,pending=0;
    for(let at=x;at+4*scale<=128;at+=step){
      const character=readCell(frame,claimed,at,y,scale,inverse,dim);
      if(character===null)break;
      if(character===' '){if(++pending>2)break;}
      else{if(pending)text+=' '.repeat(pending);pending=0;text+=character;nonblank++;}
    }
    /* Single strokes/digits stay as pixels. Two or more exact, delimited
     * characters are required; no dictionary, menu schema or OCR guessing. */
    if(nonblank<2)continue;
    const run={x,y,scale,inverse,dim,text,width:text.length*step-scale,height:5*scale};
    for(let n=0;n<text.length;n++)for(let dy=0;dy<5*scale;dy++)for(let dx=0;dx<4*scale;dx++){
      const px=x+n*step+dx,py=y+dy;claimed[py*128+px]=1;setPixel(residual,px,py,inverse);
    }
    texts.push(Object.freeze(run));
  }
}

/** Exact frame-derived model. Metadata such as currentRow never supplies text. */
export function analyzeCompanionFrame(frame,{liftText=true}={}){
  if(!validFrame(frame))throw new TypeError('A 128×64 page-addressed 1024-byte frame is required.');
  const original=frame.slice(),residual=frame.slice(),texts=[];
  if(liftText){
    const claimed=new Uint8Array(128*64);
    liftRuns(original,residual,claimed,2,false,false,texts);
    liftRuns(original,residual,claimed,2,false,true,texts);
    liftRuns(original,residual,claimed,1,true,false,texts);
    liftRuns(original,residual,claimed,1,false,false,texts);
  }
  texts.sort((a,b)=>a.y-b.y||a.x-b.x);
  return {width:128,height:64,frame:original,residual,texts,
    transcript:texts.map(run=>run.text).join('\n')};
}

/** Recompose the original binary frame, including inverse and dithered text. */
export function reconstructCompanionFrame(model){
  if(!model||!validFrame(model.residual)||!Array.isArray(model.texts))throw new TypeError('Invalid display model.');
  const frame=model.residual.slice();
  for(const run of model.texts)for(let n=0;n<run.text.length;n++){
    const bits=CANONICAL_GLYPHS[run.text[n]];
    for(let col=0;col<4;col++)for(let row=0;row<5;row++)if(bits&(1<<(col*8+row)))
      for(let dx=0;dx<run.scale;dx++)for(let dy=0;dy<run.scale;dy++){
        const x=run.x+n*5*run.scale+col*run.scale+dx,y=run.y+row*run.scale+dy;
        if(!run.dim||!((x+y)&1))setPixel(frame,x,y,!run.inverse);
      }
  }
  return frame;
}

/** Integer horizontal runs retain all remaining graph/icon/transition pixels. */
export function frameToPath(frame){
  if(!validFrame(frame))throw new TypeError('Invalid display frame.');
  const paths=[];
  for(let y=0;y<64;y++)for(let x=0;x<128;){
    if(!pixel(frame,x,y)){x++;continue;}
    const start=x;while(x<128&&pixel(frame,x,y))x++;
    const width=x-start;paths.push(`M${start} ${y}h${width}v1h-${width}z`);
  }
  return paths.join('');
}

const mounted=new WeakMap();
const sameFrame=(a,b)=>validFrame(a)&&validFrame(b)&&a.every((value,n)=>value===b[n]);
const namespace='http://www.w3.org/2000/svg';
/** Shared entry used by the embedded and expanded Web screen. No side effects
 * outside this mount, no timers/listeners, no engine/serial/MIDI operations. */
export function renderCompanionScreen(mount,snapshot){
  if(!mount?.ownerDocument)return null;
  const supplied=validFrame(snapshot?.frame),visible=supplied&&snapshot.panelVisible!==false;
  const before=mounted.get(mount);
  if(before&&before.visible===visible&&before.supplied===supplied&&(!supplied||sameFrame(before.frame,snapshot.frame)))return before.model;
  const model=supplied?analyzeCompanionFrame(snapshot.frame):null,doc=mount.ownerDocument;
  const element=(tag,attributes={})=>{const node=doc.createElementNS(namespace,tag);for(const [key,value]of Object.entries(attributes))node.setAttribute(key,String(value));return node;};
  const root=doc.createElement('div');root.className='companion-display';
  root.dataset.panel=visible?'visible':supplied?'off':'unavailable';
  const svg=element('svg',{class:'companion-display-svg',viewBox:'0 0 128 64',preserveAspectRatio:'xMidYMid meet',role:'img','aria-label':!supplied?'Display content unavailable':!visible?'Device panel is off':model.transcript||'Instrument display graphics'});
  if(visible){
    const graphics=element('path',{class:'companion-display-graphics',d:frameToPath(model.residual),'aria-hidden':'true'});svg.append(graphics);
    for(const run of model.texts){
      const text=element('text',{class:`companion-display-text${run.inverse?' is-inverse':''}${run.dim?' is-dim':''}`,
        x:run.x,y:run.y+5*run.scale,'font-size':6.7*run.scale,textLength:run.width,lengthAdjust:'spacingAndGlyphs','aria-hidden':'true'});
      text.textContent=run.text;svg.append(text);
    }
  }
  root.append(svg);mount.replaceChildren(root);
  mounted.set(mount,{frame:supplied?snapshot.frame.slice():null,visible,supplied,model});return model;
}
