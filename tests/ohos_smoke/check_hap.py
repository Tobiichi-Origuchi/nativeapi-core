#!/usr/bin/env python3
"""Reject stale variant/device outputs before installing the normal test HAP."""
import argparse
import json
import zipfile

p=argparse.ArgumentParser()
p.add_argument('hap')
p.add_argument('--desktop',choices=['0','1'],required=True)
a=p.parse_args()
with zipfile.ZipFile(a.hap) as archive:
    manifest=json.loads(archive.read('module.json'))
assert manifest['app']['bundleName']=='dev.nativeapi.smoke', 'Rebuild the normal fixture; current output is another package'
devices=manifest['module']['deviceTypes']
assert devices==['2in1'] if a.desktop=='1' else 'phone' in devices, 'Rebuild for the current guest lifecycle'
assert any(ability['name']=='EntryAbility' for ability in manifest['module']['abilities'])
print('PASS normal HAP package and device lifecycle verified')
