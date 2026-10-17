#!/usr/bin/env python3
"""Build the separate SDK-only clipboard peer without changing the main fixture."""
import argparse
import os
import subprocess
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('deveco', type=Path)
    args = p.parse_args()
    peer = Path(__file__).resolve().parent.parent / 'ohos_clipboard_peer'
    suffix = '.bat' if os.name == 'nt' else ''
    node = args.deveco / ('tools/node/node.exe' if os.name == 'nt' else 'tools/node/bin/node')
    if not node.exists():
        node = args.deveco / 'tools/node/node'
    commands = [
        [args.deveco / ('tools/ohpm/bin/ohpm' + suffix), 'install', '--all'],
        [node, args.deveco / 'tools/hvigor/bin/hvigorw.js', '--mode', 'module',
         '-p', 'product=default', 'assembleHap', '--no-daemon'],
    ]
    for command in commands:
        subprocess.run(list(map(str, command)), cwd=peer, stdin=subprocess.DEVNULL, check=True)


if __name__ == '__main__':
    main()
