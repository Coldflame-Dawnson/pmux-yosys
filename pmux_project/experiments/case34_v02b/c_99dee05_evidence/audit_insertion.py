#!/usr/bin/env python3
import json, re
from collections import Counter
from pathlib import Path
root=Path(__file__).resolve().parent
seq_types={"$adff","$dff","$dffe","$sdffce","$sdffe","$dffsr"}
def names_of(module,bits):
    return sorted(k for k,v in module["netnames"].items()
                  if any(bit in v["bits"] for bit in bits))
def canonical(module,bits):
    return [("const",bit) if isinstance(bit,str) else tuple(sorted((name,index) for name,net in module["netnames"].items() for index,value in enumerate(net["bits"]) if value==bit)) for bit in bits]
out={}
for n in (3,4):
    d=root/"insertion"/f"test{n}"
    a=json.loads((d/"before.json").read_text())["modules"][f"test{n}"]
    b=json.loads((d/"after.json").read_text())["modules"][f"test{n}"]
    ac,bc=a["cells"],b["cells"]
    removed=sorted(ac.keys()-bc.keys())
    added=sorted(bc.keys()-ac.keys())
    shared=ac.keys()&bc.keys()
    seq=sorted(k for k in shared if ac[k]["type"] in seq_types)
    direct_control_changes=[]
    raw_control_id_changes=[]
    direct_d_changes=[]
    for k in seq:
        assert ac[k]["type"]==bc[k]["type"]
        assert ac[k]["parameters"]==bc[k]["parameters"]
        assert ac[k]["attributes"]==bc[k]["attributes"]
        for port in ac[k]["connections"]:
            x,y=ac[k]["connections"][port],bc[k]["connections"][port]
            if x!=y and port!="D": raw_control_id_changes.append({"cell":k,"port":port,"before":x,"after":y,"same_named_signal":canonical(a,x)==canonical(b,y)})
            if canonical(a,x)!=canonical(b,y):
                rec={"cell":k,"port":port,"before":x,"after":y,
                     "before_names":names_of(a,x),"after_names":names_of(b,y)}
                (direct_d_changes if port=="D" else direct_control_changes).append(rec)
    narrowed=[]
    for k in shared:
        x,y=ac[k],bc[k]
        if x["type"]==y["type"]=="$mux" and x["parameters"]["WIDTH"]!=y["parameters"]["WIDTH"]:
            narrowed.append({"cell":k,"before":int(x["parameters"]["WIDTH"],2),
                             "after":int(y["parameters"]["WIDTH"],2)})
    plan=json.loads((d/"plan.json").read_text())
    stats=json.loads((d/"stats.json").read_text())
    record={"before_types":dict(Counter(c["type"] for c in ac.values())),
            "after_types":dict(Counter(c["type"] for c in bc.values())),
            "removed":removed,"added":added,"narrowed_mux":narrowed,
            "sequential_count":len(seq),"sequential_d_changes":direct_d_changes,
            "sequential_control_changes":direct_control_changes,"raw_control_id_changes":raw_control_id_changes,
            "ports_equal":a["ports"]==b["ports"],
            "init_attributes_equal":all(a["netnames"][k].get("attributes",{}).get("init")
                                          ==b["netnames"][k].get("attributes",{}).get("init")
                                          for k in a["netnames"].keys()&b["netnames"].keys()),
            "plan":plan["structural_edits"],
            "changed_structures":stats["changed_structures"],
            "check_pass":"Found and reported 0 problems." in (d/"yosys.log").read_text()}
    assert record["ports_equal"] and record["init_attributes_equal"] and record["check_pass"]
    assert record["changed_structures"]==len(record["plan"])
    if n==3:
        proof=(root/"decision_cone_case3_proof.log").read_text()
        proof_eq=set(re.findall(r"DECISION_CONE_CELL scope=source cell=(\S+) type=\$eq",proof))
        eq_changes=[]
        for k in sorted(shared):
            x,y=ac[k],bc[k]
            if x["type"]=="$eq" and (x["parameters"]!=y["parameters"] or x["connections"]!=y["connections"]):
                rec={"cell":k,"before_a":int(x["parameters"]["A_WIDTH"],2),
                     "after_a":int(y["parameters"]["A_WIDTH"],2),
                     "before_b":int(x["parameters"]["B_WIDTH"],2),
                     "after_b":int(y["parameters"]["B_WIDTH"],2)}
                eq_changes.append(rec)
        record["comparator_changes"]=eq_changes
        record["proof_eq_cells"]=sorted(proof_eq)
        assert len(added)==1 and bc[added[0]]["type"]=="$or" and not removed
        assert len(eq_changes)==3 and {x["cell"] for x in eq_changes}<=proof_eq
        assert all((x["before_a"],x["after_a"],x["before_b"],x["after_b"])==(8,7,8,7)
                   for x in eq_changes)
        assert not direct_control_changes and direct_d_changes
        assert record["plan"][0]["rule"]=="guarded_next_state_decision_cone"
        assert record["plan"][0]["added_cells"]==1
    if n==4:
        assert len(removed)==16 and all(ac[k]["type"]=="$mux" for k in removed)
        assert len(added)==26 and len(narrowed)==1
        assert narrowed[0]["before"]==2 and narrowed[0]["after"]==1
        assert record["plan"][0]["rule"]=="constant_table_cofactor"
        assert not direct_control_changes
        assert len(raw_control_id_changes)==1 and raw_control_id_changes[0]["port"]=="SRST"
        assert raw_control_id_changes[0]["same_named_signal"]
    out[f"test{n}"]=record
(root/"insertion_audit.json").write_text(json.dumps(out,indent=2)+"\n")
print(json.dumps({k:{"removed":len(v["removed"]),"added":len(v["added"]),
                     "sequential_control_changes":len(v["sequential_control_changes"]),
                     "sequential_d_changes":len(v["sequential_d_changes"]),
                     "comparator_changes":v.get("comparator_changes")} for k,v in out.items()},indent=2))