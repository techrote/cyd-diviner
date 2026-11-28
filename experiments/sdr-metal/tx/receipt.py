#!/usr/bin/env python3
"""Exact source/build receipt; no serial, flashing or RF functionality."""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess

def digest(path):
    data=path.read_bytes()
    return {"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=pathlib.Path,required=True)
    parser.add_argument('--sdk',type=pathlib.Path,required=True)
    args=parser.parse_args()
    source=pathlib.Path(__file__).resolve().parent
    sdk_sha=subprocess.check_output(['git','-C',str(args.sdk),'rev-parse','HEAD'],text=True).strip()
    if sdk_sha!='b774170ff46c393eeb5e495ea37936038d3f4f4f':raise SystemExit('Wrong SDK revision')
    files=['.gitignore','CMakeLists.txt','sdkconfig.defaults','partitions.csv','main/CMakeLists.txt','main/tx_metal.c','main/metal_words.h','test_words.c','README.md','build.ps1','receipt.py']
    artifacts=['cyd_sdr_metal_tx.bin','cyd_sdr_metal_tx.elf','cyd_sdr_metal_tx.map','config/sdkconfig.h','project_description.json']
    receipt={'schema':'cyd-sdr-metal-tx-build/1','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'status':'BUILD_EVIDENCE_ONLY_NOT_AUTHORIZED_FOR_FLASH','profile':'classic-sdr-metal-tx','sdk_commit':sdk_sha,
             'source_sha':subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip(),
             'source_tree_state':subprocess.check_output(['git','-C',str(source),'status','--porcelain'],text=True).splitlines(),
             'source_files':{name:digest(source/name) for name in files},
             'build_root':str(args.build.resolve()),'artifacts':{name:digest(args.build/name) for name in artifacts},
             'limitations':['No physical execution or RF verification','MMIO semantics inferred from pinned official classic ROM','Only exact subsequently authorized image may be flashed; approved probes unchanged']}
    target=args.build/'tx-metal-build-receipt.json'
    target.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'receipt':str(target),'firmware':receipt['artifacts']['cyd_sdr_metal_tx.bin']},indent=2))

if __name__=='__main__':main()
