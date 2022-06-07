#!/usr/bin/env python3
import os
import time
import glob
import shutil
import subprocess
from os.path import dirname, join
from pathlib import Path

ITERATION = 10

SCRIPT_DIR = dirname(__file__)
#TARGETS = ['quark', 'aqc100', 'snic', 'rtl8139']
TARGETS = ['ath9k', 'ath10k_pci', 'rtwpci']
# TARGETS = ['quark', 'aqc100', 'rtl8139']

def run_agamotto_once(target):
    with open(f"{SCRIPT_DIR}/{target}.log", 'w+', encoding='ISO-8859-1') as log:
        cmd = ['./fuzz.py', target, '-g', 'linux-prog06', '-i', 'seed/', '-N', '16']
        p = subprocess.Popen(cmd, stdout=log, stderr=log)
        return p

def pkill():
    try:
        subprocess.check_output(f'pkill qemu-system', shell=True)
    except:
        pass

def cleanup(target):
    result_dir = f'../results/{target}/prog06'
    try:
        shutil.rmtree(f'{result_dir}/out')
    except Exception as e:
        pass
    files = glob.glob(f'{result_dir}/*')
    for f in files:
        if 'overlay.qcow2' not in f:
            try:
                os.remove(f)
            except Exception as e:
                print(e)

def persist(target, iter):
    result_dir = f'../results/{target}/prog06'
    save_dir = f"/data/{os.environ.get('USER')}/agamotto/"
    Path(save_dir).mkdir(parents=True, exist_ok=True)
    shutil.move(result_dir, join(save_dir, f"{target}-{iter}"))
    Path(result_dir).mkdir()
    shutil.move(join(save_dir, f"{target}-{iter}", 'overlay.qcow2'), join(result_dir, 'overlay.qcow2'))
    shutil.move(f'{target}.log', join(save_dir, f"{target}-{iter}", f'{target}.log'))

def run_agamotto(target, iter):
    subprocess.check_call('date')
    save_dir = f"/data/{os.environ.get('USER')}/agamotto/"
    if (Path(save_dir) / f"{target}-{iter}" ).exists():
        print('Result exists: skipping')
        return
    pkill()
    cleanup(target)
    print(target)
    p = run_agamotto_once(target)
    time.sleep(60*60)
    #time.sleep(60)
    pkill()
    print(subprocess.check_output(f"./parse_log.sh {target}", shell=True).decode('utf-8'), end='')
    persist(target, iter)


for iter in range(ITERATION):
    for t in TARGETS:
        run_agamotto(t, iter)
