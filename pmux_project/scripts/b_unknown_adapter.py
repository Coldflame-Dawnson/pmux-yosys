#!/usr/bin/env python3
"""Adapter for B's UNKNOWN-only analyzer stub; accepts only reported zero writes."""
import argparse
import json
import os
import re
import subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    for key in ("before","after","plan","stats","plugin","yosys"): p.add_argument("--"+key,required=True)
    a=p.parse_args()
    plugin=Path(a.plugin).resolve();src=Path(a.before).resolve();out=Path(a.after).resolve()
    command=f'plugin -i "{plugin}"; read_rtlil "{src}"; pmux_opt; write_rtlil "{out}"'
    env=os.environ.copy();env["PATH"]=str(Path(a.yosys).parent)+os.pathsep+env.get("PATH","")
    process=subprocess.run([a.yosys,"-Q","-p",command],text=True,capture_output=True,env=env)
    log=process.stdout+process.stderr
    (out.parent/"b_unknown.log").write_text(log)
    (out.parent/"b_unknown.command.txt").write_text(command+"\n")
    if process.returncode: raise RuntimeError(f"B UNKNOWN stub rc={process.returncode}")
    match=re.search(r"pmux_opt: rewrite .*?writes=(\d+) commit=(\w+)",log)
    if not match or match.group(1)!="0" or match.group(2)!="ok":
        raise RuntimeError("B UNKNOWN stub did not report writes=0 commit=ok")
    reasons=re.findall(r"pmux_opt: reason scope=\S+ code=(\S+) count=\d+",log)
    Path(a.plan).write_text(json.dumps({"schema":1,"mode":"b-unknown","edits":[]},indent=2)+"\n")
    Path(a.stats).write_text(json.dumps({"schema":1,"mode":"b-unknown","changed_bits":0,
                                        "reason_codes":reasons},indent=2)+"\n")

if __name__=="__main__": main()
