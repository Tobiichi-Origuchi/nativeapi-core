#!/usr/bin/env python3
"""Real SDK helper-surface raster and guarded guest touch feasibility evidence."""
import argparse
import json
import math
import os
import re
import time
from pathlib import Path
from ohos_ui_fixture import Fixture

p=argparse.ArgumentParser()
p.add_argument('deveco',type=Path)
p.add_argument('--device',default='127.0.0.1:5555')
a=p.parse_args()
geometry_only=os.environ.get('NATIVEAPI_SHADOW_GEOMETRY_ONLY')=='1'
f=Fixture(a.deveco,a.device,'window-shadow','SHADOW')
assert f.instance['name'] in ['NativeAPI_Desktop','NativeAPI_Phone']
metadata=f.guest('bm','dump','-n',f.bundle)
application=json.loads(metadata[metadata.index('{'):])['applicationInfo']
assert not application['isSystemApp'] and application['appPrivilegeLevel']=='normal'
phases=['baseline','disabled-config','enable-soft','disable-soft','enable-soft-c','hard-red-c','hard-green-ui','helper-recreated','queued-off','queued-on','triangle','outside','clear-shape','moved','resized','hidden','reshown','clear','default-off','default-on','closed']
scenes=[]
def touch(x,y,expected,phase):
    tree=f.tree(phase)
    def contains(rect):
        return rect[0]<=x<rect[2] and rect[1]<=y<rect[3]
    # UITest may clip the phone main root to the band below a child, even at
    # points beside that child. Only our independently identified SDK windows
    # may use origBounds; visible foreign roots still reject the entire action.
    sdk=[state['main'],state['target']]+[h['properties'] for h in state['helpers']]
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
    picture=None if geometry_only else f.screenshot(f'{i}-{name}')
    if not geometry_only and name not in ['hidden','closed']:
        r=state['target']['windowRect']
        assert state['native']['failed']==0
        for h in state['helpers']:
            assert not h['properties']['touchable'] and not h['properties']['focusable']
        x=r['left']+r['width']//4
        y=r['top']+r['height']//4
        assert all(abs(v-t)<8 for v,t in zip(picture.getpixel((x,y)),[255,255,255] if name=='outside' else [255,0,255])),(name,'target center',picture.getpixel((x,y)))
        # Strong red core shadow immediately outside the right edge. No native
        # system shadow can satisfy this color oracle (both SDK radii are zero).
        y=r['top']+r['height']//2
        x=r['left']+(r['width']//2 if name=='triangle' else r['width'])+max(2,round(3*state['density']))
        pixel=picture.getpixel((x,y))
        if name in ['hard-red-c','hard-green-ui','helper-recreated','queued-on','triangle','clear-shape','moved','resized','reshown']:
            channel=1 if name in ['hard-green-ui','helper-recreated'] else 0
            assert pixel[channel]>249 and all(abs(pixel[j]-63)<=6 for j in range(3) if j!=channel),(name,pixel)
        elif name in ['enable-soft','enable-soft-c']:
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
        elif name not in ['clear','default-on']:
            assert min(pixel)>245,(name,pixel)
        else:
            assert max(pixel)-min(pixel)<8,(name,'default shadow must be neutral',pixel)
        touch(x,y,'main',f'{i}-{name}-shadow-pass-through')
        touch(r['left']+r['width']//4,r['top']+r['height']//4,'main' if name=='outside' else 'target',f'{i}-{name}-target-pass-through')
        state['sample']=dict(point=[x,y],rgb=pixel)
    scenes.append(state)
    (f.out/f'{i}-{name}.json').write_text(json.dumps(state,indent=2))
    f.identity()
    assert 'start ability successfully' in f.guest('aa','start','-a','EntryAbility','-b',f.bundle,'--ps','nativeapiSuite','window-shadow','--ps','nativeapiShadowAction',str(time.time_ns()))
f.wait('NATIVEAPI_SHADOW_DONE')
native=scenes[-1]['native']
assert native['failed']==0 and native['passed']>=110,native
summary=dict(guest=f.instance['name'],pid=f.pid,uid=f.uid,scenes=len(scenes),native=native,touchProbes=0 if geometry_only else 2*(len(phases)-2),geometryOnly=geometry_only,failed=0,evidence=str(f.out))
(f.out/'summary.json').write_text(json.dumps(summary,indent=2))
print('PASS',json.dumps(summary),flush=True)
