#!/usr/bin/env python3
"""Build an isolated package and reject its real notification authorization dialog."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('deveco',type=Path)
p.add_argument('--device',default='127.0.0.1:5555')
a=p.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+',a.device)
root=Path(__file__).resolve().parent
hdc=a.deveco/'sdk/default/openharmony/toolchains/hdc'
emulator=a.deveco/'tools/emulator/Emulator'
bundle='dev.nativeapi.notificationdenied'+str(time.time_ns())


def run(*cmd,env=None,timeout=60):
    return subprocess.check_output([str(x) for x in cmd],cwd=root,env=env,text=True,encoding='utf-8',
        stdin=subprocess.DEVNULL,stderr=subprocess.STDOUT,timeout=timeout)


def guest(*cmd):return run(hdc,'-t',a.device,'shell',*cmd)


assert guest('param','get','const.product.model').strip()=='emulator'
kind=guest('param','get','const.product.devicetype').strip()
instances=[x for x in json.loads(run(emulator,'-list','-details')) if x['isRunning']=='true']
assert len(instances)==1 and instances[0]['name'].startswith('NativeAPI_') and instances[0]['deviceType']==kind
assert kind in ('phone','2in1')
out=root/'test-results/notification-denial'/f'{kind}-{time.time_ns()}'
out.mkdir(parents=True)
env=dict(os.environ,NATIVEAPI_OHOS_NOTIFICATION_DENIAL='1',NATIVEAPI_OHOS_NOTIFICATION_DENIAL_BUNDLE=bundle,NATIVEAPI_OHOS_DESKTOP='1' if kind=='2in1' else '0')
# A denial build replaces the current output HAP. Force normal runners to rebuild
# rather than installing a variant through --skip-build, including after failure.
(root/'test-results/build-device-type.txt').unlink(missing_ok=True)
node=a.deveco/'tools/node/bin/node'
if not node.exists():node=a.deveco/'tools/node/node'
try:
    build=run(node,a.deveco/'tools/hvigor/bin/hvigorw.js','--mode','module','-p','product=default','assembleHap','--no-daemon',env=env,timeout=240)
except subprocess.CalledProcessError as e:
    (out/'build.log').write_text(e.output,encoding='utf-8');print(e.output);raise
(out/'build.log').write_text(build,encoding='utf-8')
hap=root/'entry/build/default/outputs/default/entry-default-unsigned.hap'
with zipfile.ZipFile(hap) as z:
    manifest=json.loads(z.read('module.json'))
assert manifest['app']['bundleName']==bundle,manifest['app']
assert manifest['module']['abilities'][0]['label']=='$string:app_denial_name'
(out/'module.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
(out/'hap-sha256.txt').write_text(hashlib.sha256(hap.read_bytes()).hexdigest()+'\n',encoding='utf-8')
shutil.copy2(hap,out/'denial.hap')
# This reserved temporary fixture is separate from dev.nativeapi.smoke. Uninstall
# only it to reset its own notification authorization for an actual refusal.
old=guest('bm','dump','-n',bundle)
if '{' in old:
    info=json.loads(old[old.index('{'):])
    assert info['applicationInfo']['bundleName']==bundle
    guest('aa','force-stop',bundle)
    assert 'success' in run(hdc,'-t',a.device,'uninstall',bundle).lower()
assert 'success' in run(hdc,'-t',a.device,'install',hap).lower()
assert 'start ability successfully' in guest('aa','start','-a','EntryAbility','-b',bundle,'--ps','nativeapiSuite','notification-denied')
time.sleep(.5)
pid=int(guest('pidof',bundle).strip())
# Show the fresh authorization UI. No API or permission-service override is used.
deadline=time.monotonic()+50
while time.monotonic()<deadline:
    log=guest('hilog','-x','-T','nativeapi-notification','-P',pid)
    (out/'runtime.log').write_text(log,encoding='utf-8')
    if 'NATIVEAPI_NOTIFICATION_AUTHORIZING' in log:
        break
    time.sleep(.2)
else:raise AssertionError('Denial fixture did not reach authorization')
setup=run(sys.executable,root/'notification_ui.py',a.deveco,'--device',a.device,
          '--bundle',bundle,'--pid',pid,'--action','dismiss-shortcut')
(out/'shortcut-ui.log').write_text(setup,encoding='utf-8')
# UI startup is asynchronous. Inspect, without accepting anything, until the exact
# app-specific system dialog exists. Keep every snapshot/ownership assertion.
for attempt in range(20):
    try:
        result=run(sys.executable,root/'notification_ui.py',a.deveco,'--device',a.device,
                   '--bundle',bundle,'--pid',pid,'--action','deny')
        (out/'deny-ui.log').write_text(result,encoding='utf-8');break
    except subprocess.CalledProcessError as e:
        current=guest('hilog','-x','-T','nativeapi-notification','-P',pid)
        (out/'runtime.log').write_text(current,encoding='utf-8')
        assert 'NATIVEAPI_NOTIFICATION_FAIL' not in current,current
        (out/f'deny-attempt-{attempt}.log').write_text(e.output,encoding='utf-8')
        if 'assert len(roots) == 1' not in e.output:
            print(e.output);raise
        time.sleep(.2)
else:raise AssertionError('The fresh refusal dialog never appeared')
for _ in range(100):
    assert guest('pidof',bundle).strip()==str(pid)
    log=guest('hilog','-x','-T','nativeapi-notification','-P',pid)
    (out/'runtime.log').write_text(log,encoding='utf-8')
    assert not re.search(r'nativeapi-notification:.*\bFAIL ',log),log
    match=re.search(r'NATIVEAPI_NOTIFICATION_PASS \((\d+) passed, 0 failed\)',log)
    if match:
        break
    time.sleep(.2)
else:raise AssertionError('Authorization refusal did not complete')
assert 'DENIED_SDK_ERROR Notification SDK error (' in log,log
(out/'case.json').write_text(json.dumps(dict(bundle=bundle,pid=pid,kind=kind,checks=int(match[1])),indent=2),encoding='utf-8')
# Success evidence is complete; remove only the temporary package.
guest('aa','force-stop',bundle)
assert 'success' in run(hdc,'-t',a.device,'uninstall',bundle).lower()
(root/f'test-results/notification-denial-{kind}-evidence.txt').write_text(str(out)+'\n',encoding='utf-8')
print(f'NATIVEAPI_NOTIFICATION_DENIAL_SUITE_PASS ({match[1]} native checks, real user refusal, PID {pid}, {kind})')
