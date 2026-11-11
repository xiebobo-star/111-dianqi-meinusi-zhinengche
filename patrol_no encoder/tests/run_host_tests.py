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
                '-DCAR_VISION_ONLY=0',
                '-Wno-unknown-pragmas', '-I'+str(root/'tests'), '-I'+str(root/'user'),
                str(root/'user/car_image.c'), str(root/'user/car_pid.c'),
                str(root/'user/car_ring.c'), str(root/'user/car_turn.c'), str(root/'user/car_control.c'),
                '-x', 'c', str(root/'tests/host_suite.c.txt'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

# Verify that scenario modules also link without the scheduler or board layer.
module_exe = out / 'patrol_module_tests.exe'
subprocess.run([a.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                '-DCAR_VISION_ONLY=0',
                '-Wno-unknown-pragmas', '-I'+str(root/'tests'), '-I'+str(root/'user'),
                str(root/'user/car_pid.c'), str(root/'user/car_ring.c'), str(root/'user/car_turn.c'),
                '-x', 'c', str(root/'tests/module_suite.c.txt'), '-o', str(module_exe)], check=True)
subprocess.run([str(module_exe)], check=True)

common = [a.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
          '-Wno-unknown-pragmas', '-I'+str(root/'tests'), '-I'+str(root/'user')]
sources = [str(root/'user'/name) for name in
           ('car_image.c', 'car_pid.c', 'car_ring.c', 'car_turn.c', 'car_control.c')]
vision_exe = out / 'vision_only_tests.exe'
subprocess.run(common + sources + ['-x', 'c', str(root/'tests/vision_only_suite.c.txt'),
                                  '-o', str(vision_exe)], check=True)
subprocess.run([str(vision_exe)], check=True)

# No encoder/IMU declarations or stubs: an accidental hardware call fails to link.
for drive in (0, 1):
    board_exe = out / f'vision_board_drive{drive}.exe'
    subprocess.run(common + ['-DHOST_BOARD_STUB', f'-DCAR_DRIVE_ENABLE={drive}'] + sources +
                   [str(root/'user/car_board.c'), '-x', 'c', str(root/'tests/board_suite.c.txt'),
                    '-o', str(board_exe)], check=True)
    subprocess.run([str(board_exe)], check=True)
