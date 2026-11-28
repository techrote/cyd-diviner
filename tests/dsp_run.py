#!/usr/bin/env python3
"""Portable DSP, exchange and actual-instrument qualification; no board/SDL needed."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitizer', choices=['none', 'address', 'thread'], default='none')
    ap.add_argument('--output-dir', type=Path, default=Path('/tmp/diviner-dsp-qualification'))
    ap.add_argument('--audio', action='store_true', help='emit eleven 24 kHz WAV specimens from actual instrument kernels')
    ap.add_argument('--compiler', default=os.environ.get('CXX', 'g++'))
    ap.add_argument('--timeout', type=float, default=180, help='maximum seconds for each compile or test process')
    args = ap.parse_args()
    if args.timeout <= 0:
        ap.error('--timeout must be positive')
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread']
    if args.sanitizer == 'address':
        flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    elif args.sanitizer == 'thread':
        flags += ['-O1', '-g', '-fsanitize=thread', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    else:
        flags += ['-O2']
    cases = [
        ('numeric-24000', 'tests/dsp_numeric_test.cpp', ['-DDIVINER_DSP_RATE=24000']),
        ('numeric-48000', 'tests/dsp_numeric_test.cpp', ['-DDIVINER_DSP_RATE=48000']),
        ('audio-exchange', 'tests/audio_exchange_test.cpp', ['-DDIVINER_DSP_RATE=24000']),
        ('instrument-kernels', 'tests/dsp_instrument_test.cpp', ['-Itests/dsp_stubs']),
        ('legacy-regression', 'tests/dsp_legacy_regression.cpp', ['-Itests/dsp_stubs']),
    ]
    env = dict(os.environ)
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    env.setdefault('TSAN_OPTIONS', 'halt_on_error=1')
    report = {'platform': platform.platform(), 'sanitizer': args.sanitizer, 'cases': [],
              'method': 'actual DSP and instrument source with graphics-free test HAL; no embedded real-time claim',
              'sanitizer_environment': {k: env[k] for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS')}}
    try:
        report['source_sha'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        report['dirty_paths'] = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).splitlines()
        report['compiler'] = subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0]
    except (OSError, subprocess.CalledProcessError) as e:
        report['setup_error'] = str(e)
        (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
        return 2
    failed = blocked = False
    for name, source, defines in cases:
        exe = out/name
        command = [args.compiler, *flags, *defines, source, '-o', str(exe)]
        started = time.monotonic()
        try:
            build = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            output = error.stdout or b''
            if isinstance(output, bytes): output = output.decode(errors='replace')
            (out/(name+'-build.log')).write_text(output+'\nFAIL: build timed out\n')
            report['cases'].append({'name': name, 'command': command, 'status': 'FAIL', 'timeout_seconds': args.timeout, 'phase': 'build'})
            failed = True
            print(name+': FAIL (build timeout)', flush=True)
            continue
        (out/(name+'-build.log')).write_text(build.stdout)
        case = {'name': name, 'command': command, 'build_exit': build.returncode}
        if build.returncode:
            case['status'] = 'FAIL'; failed = True
            print(build.stdout, end='')
        else:
            run = [str(exe)]
            if name == 'instrument-kernels' and args.audio:
                run += [str(out/'audio')]
            try:
                result = subprocess.run(run, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
            except subprocess.TimeoutExpired as error:
                output = error.stdout or b''
                if isinstance(output, bytes): output = output.decode(errors='replace')
                (out/(name+'.log')).write_text(output+'\nFAIL: test timed out\n')
                case.update({'run': run, 'status': 'FAIL', 'timeout_seconds': args.timeout, 'phase': 'test'})
                report['cases'].append(case)
                failed = True
                print(name+': FAIL (test timeout)', flush=True)
                continue
            (out/(name+'.log')).write_text(result.stdout)
            case.update({'run': run, 'exit': result.returncode})
            if result.returncode and ('ThreadSanitizer: unexpected memory mapping' in result.stdout or
                                      'LeakSanitizer has encountered a fatal error' in result.stdout):
                case['status'] = 'BLOCKED'; blocked = True
            else:
                case['status'] = 'PASS' if result.returncode == 0 else 'FAIL'
                failed |= result.returncode != 0
            print(result.stdout, end='')
        case['elapsed_seconds'] = round(time.monotonic()-started, 3)
        report['cases'].append(case)
        print(name+': '+case['status'], flush=True)
    report['status'] = 'FAIL' if failed else 'BLOCKED' if blocked else 'PASS'
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    return 1 if failed else 2 if blocked else 0

if __name__ == '__main__':
    sys.exit(main())
