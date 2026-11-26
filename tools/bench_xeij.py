#!/usr/bin/env python3
"""Run a prebuilt engine-benchmark XDF/ELF in XEiJ XVI / 16MHz under Xvfb."""
import argparse
from pathlib import Path
import shutil
import subprocess
from test_xeij import ROOT, symbols

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xeij',type=Path,required=True)
p.add_argument('--elf',type=Path,required=True)
p.add_argument('--image',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
classes=a.output/'classes';classes.mkdir(exist_ok=True)
subprocess.run(['javac','-cp',str(a.xeij.resolve()),'-d',str(classes),str(ROOT/'tools/XEiJBenchmark.java')],check=True)
manifest=a.output/'MANIFEST.MF';manifest.write_text('Premain-Class: XEiJBenchmark\n')
agent=a.output/'agent.jar'
subprocess.run(['jar','cfm',str(agent),str(manifest),'-C',str(classes),'.'],check=True)
image=a.output/'benchmark.xdf';shutil.copyfile(a.image,image)
result=a.output/'result.json';result.unlink(missing_ok=True)
command=['java',f'-javaagent:{agent}={symbols(a.elf)["bench_report"]},{result}',f'-Djava.io.tmpdir={a.output}',
         '-jar',str(a.xeij.resolve()),f'-ini={a.output/"XEiJ.ini"}','-saveonexit=off','-config=default',
         '-model=XVI','-memory=1','-clock=16','-boot=fd0',f'-fd0={image}',
         '-flrinformed=on','-usecanvas=off','-sound=off','-keyboard=none']
with (a.output/'emulator.log').open('w') as log:
    subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=200,check=True)
print(result.read_text())
