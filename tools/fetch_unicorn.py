"""Fetch the exact Unicorn source used by the recovered v0.7 runtime."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
COMMIT='8028ec436f2d9376525352dd38ed9ed6b9f6be10'
SOURCE=ROOT/'.toolchain/unicorn-source'

def main():
    SOURCE.mkdir(parents=True,exist_ok=True)
    if not (SOURCE/'.git').exists():
        subprocess.run(['git','init',str(SOURCE)],check=True)
    current=subprocess.run(['git','-C',str(SOURCE),'rev-parse','HEAD'],capture_output=True,text=True)
    if current.returncode==0 and current.stdout.strip()==COMMIT:
        print('Unicorn source already matches pinned commit');return
    if subprocess.check_output(['git','-C',str(SOURCE),'status','--porcelain'],text=True).strip():
        raise SystemExit('Unicorn source contains local changes; use a clean dependency folder.')
    subprocess.run(['git','-C',str(SOURCE),'fetch','--depth','1','https://github.com/unicorn-engine/unicorn.git',COMMIT],check=True)
    subprocess.run(['git','-C',str(SOURCE),'checkout','--detach',COMMIT],check=True)

if __name__=='__main__':main()
