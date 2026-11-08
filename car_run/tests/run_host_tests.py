"""Compile actual vision/control C against a host type shim; no ADS dependencies.
Usage: python tests/run_host_tests.py --output <directory outside ADS project>
"""
import argparse
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', required=True)
p.add_argument('--cc', default='gcc')
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(a.output).resolve()
if root == out or root in out.parents:
    raise SystemExit('Use an output directory outside the ADS project.')
out.mkdir(parents=True, exist_ok=True)
exe = out / 'patrol_host_tests.exe'
subprocess.run([a.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wno-unknown-pragmas', '-DCAR_TUNE_RUNTIME=0', '-I'+str(root/'tests'), '-I'+str(root/'user'),
                str(root/'user/car_image.c'), str(root/'user/car_pid.c'),
                str(root/'user/car_ring.c'), str(root/'user/car_turn.c'), str(root/'user/car_control.c'),
                '-x', 'c', str(root/'tests/host_suite.c.txt'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

# Verify that scenario modules also link without the scheduler or board layer.
module_exe = out / 'patrol_module_tests.exe'
subprocess.run([a.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wno-unknown-pragmas', '-DCAR_TUNE_RUNTIME=0', '-I'+str(root/'tests'), '-I'+str(root/'user'),
                str(root/'user/car_pid.c'), str(root/'user/car_ring.c'), str(root/'user/car_turn.c'),
                '-x', 'c', str(root/'tests/module_suite.c.txt'), '-o', str(module_exe)], check=True)
subprocess.run([str(module_exe)], check=True)
