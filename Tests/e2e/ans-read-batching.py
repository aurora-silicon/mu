#!/usr/bin/env python3
"""Compile real Mu data path, preserve baseline failure and verify fault artifacts."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
REL='Silicon/Apple/AppleSiliconPkg/Drivers/AppleNANDStorageDxe/Shared'
BASE='f60538a3cf327e0c08d04e438d188f102a5d4525'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('out',type=Path);a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True)
    cc=Path('/opt/homebrew/opt/llvm/bin/clang').resolve();shared=ROOT/REL;source=ROOT/'Tests/e2e/ans-read-batching.c'
    inputs=[cc,source,Path(__file__),ROOT/'Docs/j813-storage-read-batching.md',shared.parent/'AppleNANDStorageDxe.c']+list(shared.glob('*.c'))+list(shared.glob('*.h'))
    pins={str(p):sha(p) for p in inputs};rows=[]
    def run(name,directory,baseline=False,want=None):
        p=out/name;p.mkdir();cmd=[str(cc),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DSILICON_PLATFORM=8142']
        if baseline:cmd+=['-DBASELINE']
        cmd+=['-I'+str(directory),str(source)]+[str(directory/x) for x in ['AppleNvmeBlockCore.c','AppleNvmeControllerCore.c','AppleNvmeCore.c']]+['-o',str(p/'replay')]
        build=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(p/'build.log').write_text(build.stdout+build.stderr)
        rc=None;text='';err=''
        if build.returncode==0:
            result=subprocess.run([str(p/'replay'),str(p/'data.bin')],capture_output=True,text=True,timeout=10);rc=result.returncode;text=result.stdout;err=result.stderr
        (p/'stdout.jsonl').write_text(text);(p/'stderr.txt').write_text(err)
        ok=build.returncode==0 and (rc==0 if want is None else rc!=0 and want in err)
        if want is None and ok:
            golden=bytes((lba*37+i*13+(i>>8))&255 for lba in range(7,12) for i in range(4096))
            ok=(p/'data.bin').read_bytes()==golden
        row={'case':name,'pass':ok,'command':cmd,'compile_rc':build.returncode,'run_rc':rc,'expected_failure':want,'records':[json.loads(x) for x in text.splitlines()]}
        if (p/'data.bin').exists():row['data_sha256']=sha(p/'data.bin')
        rows.append(row);(p/'receipt.json').write_text(json.dumps(row,indent=2)+'\n')
    run('actual',shared)
    base=out/'baseline-source';base.mkdir()
    for p in list(shared.glob('*.c'))+list(shared.glob('*.h')):
        q=subprocess.run(['git','-C',str(ROOT),'show',BASE+':'+REL+'/'+p.name],check=True,capture_output=True);(base/p.name).write_bytes(q.stdout)
    run('baseline',base,True,'bounded sequential READ command reduction')
    mutations=[
        ('NLB','(uint32_t)(blocks - 1u)','0u','direct second page iff needed'),
        ('PRP2','device->dma_address + NTASI_ANS_DATA_ALIGN : 0','0 : 0','direct second page iff needed'),
        ('capacity','batch_bytes > device->dma_size ||','false ||','small bounce refuses'),
        ('DMA-overflow','device->dma_address > UINT64_MAX - (batch_bytes - 1u)','false','DMA overflow refuses'),
        ('reidentify','    device->read_batch_bytes = 0;\n','','reidentify revokes old batching admission'),
        ('failed-copy','        if (status != 0)\n            return status;\n        memcpy(output','        if (status != 0) {\n            memcpy(output + (size_t)index * device->media.block_size, device->dma_buffer, bytes);\n            return status;\n        }\n        memcpy(output','failed chunk and tail untouched'),
    ]
    original=(shared/'AppleNvmeBlockCore.c').read_text()
    for name,old,new,want in mutations:
        assert original.count(old)==1,(name,'unique mutation anchor')
        folder=out/('mutation-'+name+'-source');folder.mkdir()
        for p in list(shared.glob('*.c'))+list(shared.glob('*.h')):(folder/p.name).write_bytes(p.read_bytes())
        (folder/'AppleNvmeBlockCore.c').write_text(original.replace(old,new,1));run('mutation-'+name,folder,want=want)
    stable=all(sha(Path(p))==h for p,h in pins.items())
    summary={'verdict':'PASS' if stable and all(x['pass'] for x in rows) else 'FAIL','cases':rows,'source_and_compiler_sha256':pins,'stable_inputs':stable,'scope':'actual Mu block/controller code with synthetic MMIO/DMA/media; no Windows, hardware, firmware install or measured boot speed'}
    summary['artifacts_sha256']={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary['verdict'],sum(x['pass'] for x in rows),'/',len(rows));return summary['verdict']!='PASS'
if __name__=='__main__':raise SystemExit(main())
