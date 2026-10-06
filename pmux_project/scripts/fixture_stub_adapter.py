#!/usr/bin/env python3
"""External command exercising the DUT adapter; replays fixtures, no proof."""
import argparse
import json
from pathlib import Path
from run_micro import FIX, replay_fixture, yosys_json, DEFAULT_BIN

def main():
    p=argparse.ArgumentParser()
    for key in ("before","after","plan","stats","case"): p.add_argument("--"+key,required=True)
    a=p.parse_args()
    manifest={x["id"]:x for x in json.loads((FIX/"expectations.json").read_text())["cases"]}
    before=Path(a.before);work=Path(a.after).parent
    import os
    env=os.environ.copy();env["PATH"]=str(DEFAULT_BIN)+os.pathsep+env.get("PATH","")
    modules=yosys_json(before,work/"adapter_before.json",DEFAULT_BIN/"yosys",work,env,"adapter_before")
    replay_fixture(before,Path(a.after),modules,manifest[a.case],Path(a.plan),Path(a.stats))

if __name__=="__main__":main()
