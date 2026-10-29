"""Whitelisted, source-only Modal validation; private samples stay local."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import modal

ROOT = Path(os.environ.get('BSL_AUDIT_SOURCE', str(Path(__file__).resolve().parents[1])))
REMOTE = Path('/src/beat-scholar-linux')
image = (modal.Image.debian_slim(python_version='3.12')
    .apt_install('build-essential', 'cmake', 'ninja-build', 'pkg-config',
                 'libasound2-dev', 'libfontconfig1-dev', 'libfreetype6-dev',
                 'libgl1-mesa-dev', 'libgl1-mesa-dri', 'libgtk-3-dev',
                 'libx11-dev', 'libxcomposite-dev', 'libxcursor-dev',
                 'libxext-dev', 'libxinerama-dev', 'libxrandr-dev', 'libxrender-dev',
                 'fonts-lato', 'fonts-urw-base35', 'openbox', 'xauth', 'xvfb', 'xdotool', 'x11-utils')
    .pip_install('numpy==2.2.6', 'Pillow==11.3.0')
    .add_local_dir(ROOT / 'third_party/JUCE', str(REMOTE / 'third_party/JUCE'), copy=True,
                   ignore=['.git/**', 'build/**', '**/__pycache__/**']))
for name in ['actual_vst3_host.cpp', 'extract_factory_samples.cpp', 'measure_effect_parity.py',
             'migrate_factory_content.py', 'run_editor_tests.py', 'run_standalone_settings_test.py', 'modal_effect_parity_verdict.py',
             'bin/pluginval']:
    image = image.add_local_file(ROOT / 'tools' / name, str(REMOTE / 'tools' / name), copy=False)
# Mount immutable project snapshots at runtime; only stable dependencies need image layers.
image = (image
    .add_local_file(ROOT / 'CMakeLists.txt', str(REMOTE / 'CMakeLists.txt'), copy=False)
    .add_local_dir(ROOT / 'src', str(REMOTE / 'src'), copy=False)
    .add_local_dir(ROOT / 'tests', str(REMOTE / 'tests'), copy=False, ignore=['**/__pycache__/**', '*.pyc']))
app = modal.App('beat-scholar-autonomous-audit')
cache = modal.Volume.from_name('beat-scholar-linux-audit-cache', create_if_missing=True)
artifacts = modal.Volume.from_name('beat-scholar-linux-artifacts', create_if_missing=True)

@app.function(image=image, cpu=16.0, memory=49152, timeout=3600,
              volumes={'/cache': cache, '/artifacts': artifacts})
def validate(run_id: str) -> dict:
    if re.fullmatch(r'[a-zA-Z0-9_-]+', run_id) is None:
        raise ValueError('Invalid audit run ID')
    build = Path('/cache/build')
    out = Path('/artifacts/audits') / run_id
    out.mkdir(parents=True, exist_ok=False)
    results = {}
    def run(name, command):
        with (out / (name + '.log')).open('w') as log:
            proc = subprocess.run(command, cwd=REMOTE, stdout=log, stderr=subprocess.STDOUT)
        results[name] = proc.returncode
        print(f'{name}: exit {proc.returncode}', flush=True)
        if proc.returncode:
            print((out / (name + '.log')).read_text()[-12000:], flush=True)
        return proc.returncode == 0
    configured = run('configure', ['cmake', '-S', str(REMOTE), '-B', str(build), '-G', 'Ninja',
                                  '-DCMAKE_BUILD_TYPE=Release', '-DBSL_BUILD_TESTS=ON', '-DBSL_BUILD_PLUGIN=ON'])
    built = configured and run('build', ['cmake', '--build', str(build), '--clean-first', '--parallel', '12'])
    if built:
        run('ctest', ['ctest', '--test-dir', str(build), '--output-on-failure'])
        bundle = build / 'BeatScholarLinux_artefacts/Release/VST3/Beat Scholar Linux.vst3'
        if results.get('ctest') == 0:
            run('pluginval', ['xvfb-run', '-a', str(REMOTE / 'tools/bin/pluginval'),
                             '--strictness-level', '10', '--validate-in-process', '--validate', str(bundle)])
        if results.get('pluginval') == 0:
            run('standalone-settings', ['xvfb-run', '-a', '-s', '-screen 0 1600x1200x24',
                'python3', str(REMOTE / 'tools/run_standalone_settings_test.py'),
                str(build / 'BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux'),
                str(out / 'standalone-settings')])
        if (build / 'editor-regression').is_dir():
            shutil.copytree(build / 'editor-regression', out / 'editor-regression')
        if (build / 'actual-vst3-smoke/report.json').is_file():
            shutil.copy2(build / 'actual-vst3-smoke/report.json', out / 'actual-vst3-report.json')
        module = bundle / 'Contents/x86_64-linux/Beat Scholar Linux.so'
        if module.is_file():
            results['module_sha256'] = hashlib.sha256(module.read_bytes()).hexdigest()
            with tarfile.open(out / 'candidate-vst3.tar.gz', 'w:gz') as archive:
                archive.add(bundle, arcname=bundle.name)
            # Export matching verification binaries before another run changes the cache.
            with tarfile.open(out / 'verification-binaries.tar.gz', 'w:gz') as archive:
                for name, path in [('factory-playback-tests', build / 'beat_scholar_factory_playback_tests'),
                                   ('Beat Scholar Linux', build / 'BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux')]:
                    archive.add(path, arcname=name)
                    results[name + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    results['private_factory_test'] = 'not available remotely; must run locally'
    results['artifact_path'] = str(out.relative_to('/artifacts'))
    results['source_sha256'] = {str(p.relative_to(REMOTE)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for subtree in ['src', 'tests'] for p in (REMOTE / subtree).rglob('*')
                               if p.is_file() and p.suffix in ('.cpp', '.h', '.py')}
    (out / 'result.json').write_text(json.dumps(results, indent=2) + '\n')
    cache.commit()
    artifacts.commit()
    return results

@app.function(image=image, cpu=2.0, memory=2048, timeout=180,
              volumes={'/cache': cache, '/artifacts': artifacts})
def probe_standalone(run_id: str) -> dict:
    if re.fullmatch(r'[a-zA-Z0-9_-]+', run_id) is None:
        raise ValueError('Invalid audit run ID')
    out = Path('/artifacts/audits') / run_id
    out.mkdir(parents=True, exist_ok=False)
    with (out / 'probe.log').open('w') as log:
        result = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1600x1200x24',
            'python3', str(REMOTE / 'tools/run_standalone_settings_test.py'),
            '/cache/build/BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux', str(out)],
            stdout=log, stderr=subprocess.STDOUT)
    artifacts.commit()
    return {'standalone_settings': result.returncode, 'artifact_path': str(out.relative_to('/artifacts'))}

@app.function(image=image, cpu=16.0, memory=49152, timeout=900,
              volumes={'/cache': cache, '/artifacts': artifacts})
def diagnose_startup(run_id: str) -> dict:
    if re.fullmatch(r'[a-zA-Z0-9_-]+', run_id) is None:
        raise ValueError('Invalid diagnostic run ID')
    out = Path('/artifacts/audits') / run_id
    out.mkdir(parents=True, exist_ok=False)
    commands = {
        'configure': ['cmake', '-S', str(REMOTE), '-B', '/cache/build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBSL_BUILD_TESTS=ON', '-DBSL_BUILD_PLUGIN=ON'],
        'build-standalone': ['cmake', '--build', '/cache/build', '--target', 'BeatScholarLinux_Standalone', '--clean-first', '--parallel', '12'],
        'startup': ['xvfb-run', '-a', '-s', '-screen 0 1600x1200x24', 'python3', str(REMOTE / 'tools/run_standalone_settings_test.py'), '/cache/build/BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux', str(out)]}
    result = {}
    for name, command in commands.items():
        with (out / (name + '.log')).open('w') as log:
            proc = subprocess.run(command, cwd=REMOTE, stdout=log, stderr=subprocess.STDOUT, timeout=600 if name == 'build-standalone' else 150)
        result[name] = proc.returncode
        print(name, proc.returncode, flush=True)
        if proc.returncode:
            print((out / (name + '.log')).read_text()[-4000:], flush=True)
            break
    if (out / 'application.log').exists():
        print((out / 'application.log').read_text()[-6000:], flush=True)
    cache.commit()
    artifacts.commit()
    result['artifact_path'] = str(out.relative_to('/artifacts'))
    return result

@app.local_entrypoint()
def main(run_id: str, standalone_only: bool = False, startup_diagnostic: bool = False):
    result = diagnose_startup.remote(run_id) if startup_diagnostic else (probe_standalone.remote(run_id) if standalone_only else validate.remote(run_id))
    print(json.dumps(result, indent=2), flush=True)
