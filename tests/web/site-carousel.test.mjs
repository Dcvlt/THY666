import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

test('screen carousel selects and wraps actual C examples using buttons and arrow keys', async () => {
  execFileSync(process.platform === 'win32' ? 'python' : 'python3', ['tools/build_site.py'], {stdio:'pipe'});
  const original = globalThis.document;
  const node = () => ({attributes:{},listeners:{},children:[],setAttribute(k,v){this.attributes[k]=v;},addEventListener(k,v){this.listeners[k]=v;},append(v){this.children.push(v);}});
  const elements = Object.fromEntries(['img','figcaption','[data-view-count]','[data-view-dots]','[data-view-prev]','[data-view-next]'].map(key=>[key,node()]));
  const mount = {...node(),querySelector:key=>elements[key]};
  globalThis.document={querySelectorAll:()=>[mount],createElement:()=>node()};
  try {
    const {screenViews}=await import('../../build/site/screen-carousel.js');
    assert.equal(screenViews.length,3);
    assert.equal(elements.img.src,screenViews[0].src);
    elements['[data-view-prev]'].listeners.click();
    assert.equal(elements.img.src,screenViews[2].src);
    elements['[data-view-next]'].listeners.click();
    assert.equal(elements.img.src,screenViews[0].src);
    elements['[data-view-dots]'].children[1].listeners.click();
    assert.equal(elements.img.src,screenViews[1].src);
    let prevented=false;mount.listeners.keydown({key:'ArrowRight',preventDefault(){prevented=true;}});
    assert.equal(prevented,true);assert.equal(elements.img.src,screenViews[2].src);
    assert.equal(elements['[data-view-dots]'].children[2].attributes['aria-pressed'],'true');
    assert.equal(elements['[data-view-count]'].textContent,'3 / 3');
  } finally {globalThis.document=original;}
});
