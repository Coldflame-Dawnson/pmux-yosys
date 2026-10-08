#!/usr/bin/env python3
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parent
seed_file=root.parents[1]/"tests/pmux_m1/seeds_1000.txt"
seeds=[int(line) for line in seed_file.read_text().splitlines() if line.strip()]
assert seeds==list(range(1000))
results={}
for mode,path in (("default",root/"random_default"),("v02",root/"random_v02")):
    summary=json.loads((path/"summary.json").read_text())
    assert (summary["count"],summary["pass"],summary["fail"])==(1000,1000,0)
    bad=[row["seed"] for row in summary["rows"] if row["result"]!="PASS"]
    rec={"count":summary["count"],"pass":summary["pass"],"fail":summary["fail"],
         "failure_seeds":bad,"structure_correct":summary["structure_correct"],
         "eqy_pass":summary["eqy_pass"],"synthesis_pass":summary["synthesis_pass"]}
    if mode=="v02":
        matches=[]
        no_candidate=0
        for seed in seeds:
            d=path/f"seed_{seed:08d}"
            plan=json.loads((d/"plan.json").read_text())
            stats=json.loads((d/"stats.json").read_text())
            if stats.get("v02_case34",{}).get("enabled") is not True:
                raise AssertionError(f"seed {seed} v02 disabled")
            edits=plan.get("structural_edits",[])
            if edits:
                matches.append(seed)
            else:
                assert stats.get("changed_structures")==0
                assert "no_v02_case34_candidate" in stats.get("reason_codes",[])
                no_candidate+=1
        rec["structural_match_seeds"]=matches
        rec["no_candidate_count"]=no_candidate
        assert no_candidate==1000 and not matches
    results[mode]=rec
results["seed_sha256"]=hashlib.sha256(seed_file.read_bytes()).hexdigest()
(root/"random_audit.json").write_text(json.dumps(results,indent=2)+"\n")
print(json.dumps(results,indent=2))