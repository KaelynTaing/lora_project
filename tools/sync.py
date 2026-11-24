#!/usr/bin/env python3
"""Arduino sketches can't share files, so this copies shared/*.h into every sketch folder.
Edit files in shared/ only, then run:  python3 tools/sync.py"""
import pathlib, shutil
root = pathlib.Path(__file__).resolve().parent.parent
shared = sorted((root / "shared").glob("*.h"))
for sk in ["atecc_setup", "node_a_72h", "node_b_72h", "node_twoway", "compass_test", "cloud_test"]:
    d = root / sk
    if d.is_dir():
        for f in shared:
            shutil.copy2(f, d / f.name)
        print("synced", sk)
