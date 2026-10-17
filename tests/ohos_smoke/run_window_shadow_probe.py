#!/usr/bin/env python3
"""Real SDK helper-surface raster and guarded guest touch feasibility evidence."""
import argparse
import json
import math
import re
import time
from pathlib import Path
from ohos_ui_fixture import Fixture

p=argparse.ArgumentParser()
p.add_argument('deveco',type=Path)
p.add_argument('--device',default='127.0.0.1:5555')
a=p.parse_args()
f=Fixture(a.deveco,a.device,'window-shadow-probe','SHADOW')
assert f.instance['name'] in ['NativeAPI_Desktop','NativeAPI_Phone']
phases=['baseline','soft-on','off','hard-offset','moved','hidden','reshown','destroyed']
scenes=[]
def touch(x,y,expected,phase):
    tree=f.tree(phase)
    def contains(rect):
        return rect[0]<=x<rect[2] and rect[1]<=y<rect[3]
    # UITest may clip the phone main root to the band below a child, even at
    # points beside that child. Only our independently identified SDK windows
    # may use origBounds; visible foreign roots still reject the entire action.
    sdk=[state['main'],state['target'],state['helper']]
    known={w['id']:w for w in sdk if w}
    hits=[]
    for root in tree['children']:
        attrs=root['attributes']
        own=attrs.get('bundleName')==f.bundle
        if not own:
            assert not contains(f.rect(root)),('foreign window covers press point',attrs)
            continue
        original=list(map(int,re.findall(r'-?\d+',attrs.get('origBounds',attrs['bounds']))))
        assert len(original)==4
        if contains(original):
            identifier=int(attrs['hostWindowId'])
            assert identifier in known,('unexpected same-bundle window',identifier)
            rect=known[identifier]['windowRect']
            assert contains([rect['left'],rect['top'],rect['left']+rect['width'],rect['top']+rect['height']])
            hits.append(root)
    assert hits,('no verified own SDK window at press point',x,y)
    rows=f.windows(phase)
    for root in hits:
        row=rows[int(root['attributes']['hostWindowId'])]
        assert row[2]==f.pid and int(row[7])>0
    f.identity()
    before=re.findall(r'NATIVEAPI_SHADOW_TOUCH (main|target)',f.logs())
    (f.out/f'{phase}-owner.json').write_text(json.dumps(dict(point=[x,y],pid=f.pid,uid=f.uid,roots=[r['attributes'] for r in hits],expected=expected),indent=2))
    assert 'Scenario simulation success' in f.run(f.emulator,'-instance',f.instance['name'],'-click',f'{x} {y}')
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        after=re.findall(r'NATIVEAPI_SHADOW_TOUCH (main|target)',f.logs())
        if len(after)>len(before):
            assert after==before+[expected],after
            return
        time.sleep(.1)
    raise AssertionError(('Missing guarded real touch',phase,expected))

for i,name in enumerate(phases):
    marker=f'NATIVEAPI_SHADOW_PHASE {i} {name} '
    log=f.wait(marker)
    state=json.loads(next(line.split(marker,1)[1] for line in log.splitlines() if marker in line))
    picture=f.screenshot(f'{i}-{name}')
    if name not in ['hidden','destroyed']:
        r=state['target']['windowRect']; h=state['helper']
        assert not h['touchable'] and not h['focusable']
        assert state['focus']==state['initialFocus'],state
        x=r['left']+r['width']-2
        y=r['top']+r['height']//2
        assert all(abs(v-t)<8 for v,t in zip(picture.getpixel((x,y)),[255,0,255])),(name,'target center',picture.getpixel((x,y)))
        # Strong red core shadow immediately outside the right edge. No native
        # system shadow can satisfy this color oracle (both SDK radii are zero).
        x=r['left']+r['width']+max(2,round(3*state['density']))
        pixel=picture.getpixel((x,y))
        if name in ['hard-offset','moved','reshown']:
            assert pixel[0]>245 and 45<=pixel[1]<=80 and abs(pixel[1]-pixel[2])<6,(name,pixel)
        elif name=='soft-on':
            # Independent one-dimensional convolution count, not the production
            # raster algorithm: at this scanline the vertical kernel lies wholly
            # inside the rectangle. Count three uniform horizontal box samples.
            radius=math.ceil(18*state['density']/3)
            shifted=math.floor(12*state['density']+.5)
            sample=x-r['left']
            count=0
            for k1 in range(-radius,radius+1):
                for k2 in range(-radius,radius+1):
                    low=max(-radius,shifted-sample-k1-k2)
                    high=min(radius,shifted+r['width']-1-sample-k1-k2)
                    count+=max(0,high-low+1)
            expected_alpha=round(192*count/(2*radius+1)**3)
            expected_green=255-expected_alpha
            assert 0<expected_alpha<192
            assert pixel[0]>249 and abs(pixel[1]-expected_green)<=4 and abs(pixel[1]-pixel[2])<=2,(name,pixel,expected_green)
            state['softOracle']=dict(radius=radius,expectedGreen=expected_green,actualGreen=pixel[1],tolerance=4)
        else:
            assert min(pixel)>245,(name,pixel)
        touch(x,y,'main',f'{i}-{name}-shadow-pass-through')
        touch(r['left']+r['width']//2,y,'target',f'{i}-{name}-target-pass-through')
        state['sample']=dict(point=[x,y],rgb=pixel)
    scenes.append(state)
    (f.out/f'{i}-{name}.json').write_text(json.dumps(state,indent=2))
    f.identity()
    assert 'start ability successfully' in f.guest('aa','start','-a','EntryAbility','-b',f.bundle,'--ps','nativeapiSuite','window-shadow-probe','--ps','nativeapiShadowAction',str(time.time_ns()))
f.wait('NATIVEAPI_SHADOW_DONE')
summary=dict(guest=f.instance['name'],pid=f.pid,uid=f.uid,scenes=len(scenes),touchProbes=12,failed=0,evidence=str(f.out))
(f.out/'summary.json').write_text(json.dumps(summary,indent=2))
print('PASS',json.dumps(summary),flush=True)
