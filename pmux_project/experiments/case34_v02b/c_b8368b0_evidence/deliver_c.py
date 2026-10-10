#!/usr/bin/env python3
"""Package and independently verify C evidence after commit and remote push."""
import argparse,datetime,hashlib,json,shutil,subprocess,zipfile
from pathlib import Path
E=Path(__file__).resolve().parent;R=E.parents[3];O=R/'pmux_project/reports/c-b8368b0-default-final'
p=argparse.ArgumentParser();p.add_argument('--remote-head',required=True);args=p.parse_args()
D=Path('/mnt/c/Users/15091/Desktop/Yosys_简单电路全流程例子_朱跃文/PMUX_C_b8368b0_最终验收_2026-10-10');D.mkdir(exist_ok=True)
def git(*args):return subprocess.check_output(['git',*args],cwd=R,text=True).strip()
def sha_bytes(b):return hashlib.sha256(b).hexdigest()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb')as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
head=git('rev-parse','HEAD');branch=git('branch','--show-current');status=git('status','--porcelain')
assert head==args.remote_head==git('rev-parse','refs/remotes/origin/'+branch) and not status
assert git('rev-parse','HEAD^')=='b8368b0f3e2d55a2bb429296a3aea859eb4f2049'
assert not git('diff','HEAD^','HEAD','--','pmux_project/pass','techlibs/intel','pmux_project/include','pmux_project/analyzer','pmux_project/tests','pmux_project/pmux_case')
checks={}
for line in (E/'SHA256SUMS').read_text().splitlines():
 digest,path=line.split('  ',1);assert sha(R/path)==digest,path;checks[path]=digest
inventory=O/'RAW_INVENTORY.jsonl';raw=[json.loads(s) for s in inventory.read_text().splitlines()]
summary=json.loads((E/'raw_inventory_summary.json').read_text());assert len(raw)==summary['files'] and sha(inventory)==summary['sha256']
tracked=git('diff-tree','--no-commit-id','--name-only','-r','HEAD').splitlines()
# Copy small records for convenient review, with paths matching repository-relative hashes.
for rel in tracked:
 dest=D/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(R/rel,dest)
name='C_b8368b0_complete_evidence.zip';zpath=D/name;done=set();count=0
with zipfile.ZipFile(zpath,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=1,allowZip64=True)as z:
 for item in raw:
  rel=item['path'];data=(R/rel).read_bytes();assert len(data)==item['size'] and sha_bytes(data)==item['sha256'],rel
  z.writestr(rel,data);done.add(rel);count+=1
  if count%25000==0:print('ZIP_VERIFIED',count,flush=True)
 for rel in sorted(set(tracked+[str(inventory.relative_to(R))])):
  if rel not in done:z.write(R/rel,rel);done.add(rel)
print('ZIP_WRITTEN',zpath.stat().st_size,flush=True)
with zipfile.ZipFile(zpath)as z:
 assert z.testzip() is None
 # Each zipped raw file is verified by SHA, independently of CRC.
 for i,item in enumerate(raw,1):
  assert sha_bytes(z.read(item['path']))==item['sha256'],item['path']
  if i%50000==0:print('ZIP_SHA_CHECK',i,flush=True)
 for rel,digest in checks.items():assert sha_bytes(z.read(rel))==digest,rel
zsha=sha(zpath)
verification={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'branch':branch,'C_commit':head,'B_commit':git('rev-parse','HEAD^'),'A_authorization_commit':'d03381f4511ab520372f3c2e57657b141a4959bc','remote_HEAD':args.remote_head,'remote_tracking_HEAD':git('rev-parse','refs/remotes/origin/'+branch),'worktree_status':status,'production_diff':'','files_changed':tracked,'raw_files_verified':len(raw),'tracked_checksum_entries_verified':len(checks),'zip_crc':'PASS','zip_all_raw_sha256':'PASS','zip_bytes':zpath.stat().st_size,'zip_sha256':zsha,'raw_inventory_sha256':sha(inventory),'G3':'PASS'}
(D/'DELIVERY_VERIFICATION.json').write_text(json.dumps(verification,ensure_ascii=False,indent=2)+'\n')
(D/'DELIVERY_SHA256SUMS').write_text(f'{zsha}  {name}\n{sha(D/"DELIVERY_VERIFICATION.json")}  DELIVERY_VERIFICATION.json\n')
(D/'README.md').write_text(f'''# C 最终验收交付

G3 = PASS。

C 分支：`{branch}`。完整提交：`{head}`，远端 HEAD 已核对一致，工作树干净。精确 B：`{verification['B_commit']}`；A 授权：`{verification['A_authorization_commit']}`。

最终报告：[C_DEFAULT_PATH_FINAL_ACCEPTANCE_b8368b0.md](pmux_project/experiments/case34_v02b/C_DEFAULT_PATH_FINAL_ACCEPTANCE_b8368b0.md)。小型结果、MANIFEST.md、SHA256SUMS 和文件变更清单在 c_b8368b0_evidence 子目录。

完整原始证据 ZIP：`{name}`，{zpath.stat().st_size} 字节，SHA-256 `{zsha}`。归档中 {len(raw)} 份原始文件均逐文件 SHA-256 校验，ZIP CRC 通过。解压后路径与仓库相对路径一致。保留全部 EQY work/PASS、gold/gate、命令、日志、工具、结构 JSON、种子逐项结果和准备阶段失败记录。

最终 58/58 生命周期、19/19 微测试、26/26 mutation、Analyzer fast/slow、默认及显式各 1000/1000、四例 whole-design EQY 均 PASS。准备阶段夹具编译/覆盖/后端问题及一份未单独保存的临时编译日志在报告中逐项披露，不冒充最终 gate 结果。未修改生产代码、接口、RTL、测试期望或 oracle，未合并主分支。
''')
assert git('status','--porcelain')==''
print('DELIVERY_PASS',head,zsha,flush=True)