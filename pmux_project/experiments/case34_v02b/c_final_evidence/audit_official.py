#!/usr/bin/env python3
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parent
repo=root.parents[2]
official=repo/"pmux_project/pmux_case/competition_case"
matrix=root/"official_matrix"
summary=json.loads((matrix/"summary.json").read_text())
oracle={"test1":(288,222,288,222),"test2":(25,0,25,0),
        "test3":(13,17,11,12),"test4":(36,10,27,10)}
source_sha={"test1":"2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13",
            "test2":"e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146",
            "test3":"1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9",
            "test4":"c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7"}
out={}
for top,expected in oracle.items():
    source=official/f"{top}.v"
    gold_sha=hashlib.sha256(source.read_bytes()).hexdigest()
    assert gold_sha==source_sha[top]==summary["cases"][top]["gold_sha256"]
    variants={}
    for variant in ("baseline","default","custom"):
        d=matrix/top/variant
        module=json.loads((d/"post_abc9.json").read_text())["modules"][top]
        types={}
        for cell in module["cells"].values():
            types[cell["type"]]=types.get(cell["type"],0)+1
        io={"input":0,"output":0,"inout":0}
        for port in module["ports"].values():
            io[port["direction"]]+=len(port["bits"])
        plan=json.loads((d/"plan.json").read_text())
        stats=json.loads((d/"stats.json").read_text())
        eqy_pass=(d/"gate_eqy/work/PASS").is_file() and "DONE (PASS" in (d/"gate_eqy/eqy.log").read_text()
        log=(d/"yosys.log").read_text()
        assert eqy_pass and "Found and reported 0 problems." in log
        assert stats["changed_structures"]==len(plan.get("structural_edits",[]))
        if variant!="baseline":
            assert stats["v02_case34"]["enabled"] is True
        rec={"lut":types.get("$lut",0),"dff":types.get("dffeas",0),
             "io_bits":io,"total_cells":sum(types.values()),
             "eqy_pass":eqy_pass,"gate_sha256":hashlib.sha256((d/"post_abc9.il").read_bytes()).hexdigest(),
             "structural_edits":plan.get("structural_edits",[]),
             "decision_cone_candidates":stats["v02_case34"]["decision_cone_candidates"],
             "committed_candidates":stats["v02_case34"]["committed_candidates"],
             "reason_codes":stats["reason_codes"],
             "timing":summary["cases"][top]["variants"][variant]["timing"]}
        variants[variant]=rec
    base=variants["baseline"]
    assert (base["lut"],base["dff"])==expected[:2]
    assert all(v["io_bits"]==base["io_bits"] for v in variants.values())
    for candidate in ("default","custom"):
        c=variants[candidate]
        if top in ("test1","test2"):
            assert c["decision_cone_candidates"]==0 and c["structural_edits"]==[]
            assert "no_v02_case34_candidate" in c["reason_codes"]
        elif top=="test3":
            assert c["decision_cone_candidates"]==c["committed_candidates"]==1
            assert len(c["structural_edits"])==1
            assert c["structural_edits"][0]["rule"]=="guarded_next_state_decision_cone"
            assert c["structural_edits"][0]["added_cells"]==1
        else:
            assert c["decision_cone_candidates"]==0
            assert len(c["structural_edits"])==1
            edit=c["structural_edits"][0]
            assert (edit["rule"],edit["removed_cells"],edit["added_cells"])==("constant_table_cofactor",16,26)
            assert len(edit["split_by_output_bit"])==10
    selected=summary["cases"][top]["selected"]
    assert selected==("baseline" if top in ("test1","test2") else "custom")
    final=variants[selected]
    assert final["lut"]<=expected[2] and final["dff"]<=expected[3]
    assert final["eqy_pass"] and final["io_bits"]==base["io_bits"]
    out[top]={"gold_sha256":gold_sha,"variants":variants,"selected":selected,
              "final":final,"oracle":expected}
(root/"official_audit.json").write_text(json.dumps(out,indent=2)+"\n")
print(json.dumps({k:{"selected":v["selected"],"lut":v["final"]["lut"],
                     "dff":v["final"]["dff"],"eqy":v["final"]["eqy_pass"]}
                  for k,v in out.items()},indent=2))