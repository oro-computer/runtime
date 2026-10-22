#!/usr/bin/env python3
"""Read-only release evidence: signature, manifest, assets, ZIP and ELF alignment.

This does not install an APK or claim device, TLS, accessibility or performance
qualification. Supply a trusted signing certificate digest from release policy.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def elf_segments(data):
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 library')
    machine = struct.unpack_from('<H', data, 18)[0]
    offset = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size < 56 or not 0 < count <= 4096 or offset + size * count > len(data):
        raise ValueError('Invalid ELF program header table')
    loads = []
    for index in range(count):
        kind, _, file_offset, address, _, file_size, memory_size, alignment = struct.unpack_from(
            '<IIQQQQQQ', data, offset + index * size)
        if kind != 1:
            continue
        if (alignment < 16384 or alignment & (alignment - 1) or
                file_offset % 16384 != address % 16384 or file_size > memory_size or
                file_offset + file_size > len(data)):
            raise ValueError('ELF PT_LOAD segment is invalid or incompatible with 16 KiB pages')
        loads.append({'offset': file_offset, 'address': address, 'alignment': alignment})
    if not loads:
        raise ValueError('ELF library has no loadable segments')
    return machine, loads


def native_archive_evidence(manifest, runtime_abis):
    archives = manifest.get('nativeArchives')
    if not isinstance(archives, list) or not archives:
        raise ValueError('Native archive provenance is missing')
    paths = set()
    for entry in archives:
        if not isinstance(entry, dict):
            raise ValueError('Invalid native archive evidence')
        path, digest = entry.get('path'), entry.get('sha256')
        if (not isinstance(path, str) or not re.fullmatch(
                r'[A-Za-z0-9_-]+-android/lib[A-Za-z0-9_.-]+\.a', path) or path in paths):
            raise ValueError('Duplicate or unsafe native archive path')
        if not isinstance(digest, str) or not re.fullmatch('[a-f0-9]{64}', digest):
            raise ValueError('Invalid native archive SHA-256')
        paths.add(path)
    for abi in runtime_abis:
        if f'{abi}-android/liboro-runtime.a' not in paths:
            raise ValueError(f'Native runtime archive provenance is missing for {abi}')
    return archives


def application_input_evidence(manifest):
    if not isinstance(manifest, dict):
        raise ValueError('Invalid native application build evidence')
    inputs = manifest.get('inputs', {})
    if not isinstance(inputs, dict) or not isinstance(inputs.get('androidDistribution'), dict):
        raise ValueError('Native application runtime contract is missing')
    native_archive_evidence(manifest, set())
    evidence = inputs.get('applicationInputs')
    if not isinstance(evidence, dict) or not isinstance(evidence.get('inventory'), dict):
        raise ValueError('Native application input provenance is missing')
    inventory = evidence['inventory']
    if (type(inventory.get('schema')) is not int or inventory['schema'] != 1 or
            inventory.get('scope') != 'staged-android-inputs-v1' or
            type(inventory.get('debug')) is not bool or inventory['debug'] != inputs.get('debug')):
        raise ValueError('Invalid native application input schema or build mode')
    for name in ['configurationSha256', 'runtimeContractSha256']:
        if not isinstance(inventory.get(name), str) or not re.fullmatch('[a-f0-9]{64}', inventory[name]):
            raise ValueError('Invalid native application input digest')
    def canonical(value):
        return json.dumps(value, ensure_ascii=False, sort_keys=True,
                          separators=(',', ':')).encode('utf-8')
    if inventory['runtimeContractSha256'] != sha256(canonical(inputs.get('androidDistribution'))):
        raise ValueError('Native application runtime contract differs')
    if inventory.get('nativeArchives') != manifest.get('nativeArchives'):
        raise ValueError('Native application archive inventory differs')
    if inventory.get('toolchain') != {name: inputs.get(name) for name in ['sdk', 'nativePlatform', 'ndkVersion']}:
        raise ValueError('Native application toolchain inputs differ')
    if any(not isinstance(inputs.get(name), str) or not 0 < len(inputs[name]) <= 128
           for name in ['sdk', 'nativePlatform', 'ndkVersion']):
        raise ValueError('Native application toolchain versions are missing')
    files = inventory.get('files')
    if not isinstance(files, list) or not 0 < len(files) <= 100000:
        raise ValueError('Native application file inventory is missing or exceeds its limit')
    paths, total = [], 0
    for entry in files:
        if not isinstance(entry, dict):
            raise ValueError('Invalid native application file declaration')
        path, digest, size = entry.get('path'), entry.get('sha256'), entry.get('size')
        if (not isinstance(path, str) or not 0 < len(path) <= 4096 or '\\' in path or
                path.startswith('/') or ':' in path or
                any(part in ['', '.', '..'] for part in path.split('/'))):
            raise ValueError('Unsafe native application input path')
        if (not isinstance(digest, str) or not re.fullmatch('[a-f0-9]{64}', digest) or
                type(size) is not int or size < 0 or entry.get('normalization') not in ['bytes', 'build-paths']):
            raise ValueError('Invalid native application input declaration')
        total += size
        paths.append(path)
    if total > 8589934592 or paths != sorted(set(paths)):
        raise ValueError('Native application input inventory is duplicated, unsorted or exceeds 8 GiB')
    if evidence.get('sha256') != sha256(canonical(inventory)):
        raise ValueError('Native application input fingerprint mismatch')
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--sdk', type=Path, default=os.environ.get('ANDROID_HOME'))
    parser.add_argument('--build-tools', default='36.0.0')
    parser.add_argument('--certificate-sha256', required=True,
                        help='Trusted release signing certificate SHA-256 (not obtained from this APK)')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    expected_certificate = args.certificate_sha256.replace(':', '').lower()
    if not re.fullmatch('[a-f0-9]{64}', expected_certificate):
        parser.error('--certificate-sha256 must be a SHA-256 certificate digest')
    if not args.sdk:
        parser.error('--sdk or ANDROID_HOME is required')
    checks = []
    with args.apk.open('rb') as stream:
        apk_digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    report = {'schema': 1, 'apkSha256': apk_digest, 'checks': checks}

    def check(name, operation):
        try:
            result = operation()
            checks.append({'name': name, 'passed': True, 'evidence': result})
        except (OSError, ValueError, KeyError, ET.ParseError, zipfile.BadZipFile,
                struct.error, subprocess.SubprocessError) as error:
            checks.append({'name': name, 'passed': False, 'error': str(error)})

    def run(tool, *arguments):
        result = subprocess.run([str(tool), *map(str, arguments)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise ValueError(f'{tool.name} exited {result.returncode}')
        return result.stdout

    tools = args.sdk / 'build-tools' / args.build_tools

    def signature():
        output = run(tools / 'apksigner', 'verify', '--verbose', '--print-certs', args.apk)
        digests = re.findall(r'^Signer #[0-9]+ certificate SHA-256 digest: ([a-fA-F0-9]+)$', output, re.M)
        if not digests or any(value.lower() != expected_certificate for value in digests):
            raise ValueError('APK signer does not match the trusted release certificate')
        subjects = re.findall(r'^Signer #[0-9]+ certificate DN: (.*)$', output, re.M)
        if any('Android Debug' in subject for subject in subjects):
            raise ValueError('APK uses an Android debug certificate')
        return {'certificateSha256': expected_certificate, 'verified': True}

    check('signature', signature)
    check('zipAlignment', lambda: run(tools / 'zipalign', '-c', '-P', '16', '-v', '4', args.apk).splitlines()[-1])

    def manifest():
        output = run(args.sdk / 'cmdline-tools/latest/bin/apkanalyzer', 'manifest', 'print', args.apk)
        root = ET.fromstring(output)
        app = root.find('application')
        ns = '{http://schemas.android.com/apk/res/android}'
        if app is None or app.get(ns + 'debuggable', 'false') != 'false':
            raise ValueError('Release application is missing or debuggable')
        if app.get(ns + 'allowBackup') != 'false' or app.get(ns + 'usesCleartextTraffic') != 'false':
            raise ValueError('Release must explicitly disable backup and cleartext traffic')
        sdk = root.find('uses-sdk')
        minimum = int(sdk.get(ns + 'minSdkVersion', '0')) if sdk is not None else 0
        target = int(sdk.get(ns + 'targetSdkVersion', '0')) if sdk is not None else 0
        if minimum < 26 or target < minimum:
            raise ValueError('Release must declare supported minimum/target SDK levels (minimum 26)')
        permissions = sorted(item.get(ns + 'name') for item in root.findall('uses-permission'))
        return {'package': root.get('package'), 'permissions': permissions, 'debuggable': False,
                'minSdk': minimum, 'targetSdk': target}

    check('releaseManifest', manifest)

    with zipfile.ZipFile(args.apk) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('APK contains duplicate ZIP entries')
        native = {}

        def libraries():
            result = []
            for name in names:
                if not name.startswith('lib/') or not name.endswith('.so'):
                    continue
                info = archive.getinfo(name)
                if info.file_size > 268435456:
                    raise ValueError('Native library exceeds 256 MiB audit bound')
                data = archive.read(name)
                machine, segments = elf_segments(data)
                abi = name.split('/')[1]
                if {'arm64-v8a': 183, 'x86_64': 62}.get(abi) != machine:
                    raise ValueError('Native ABI and ELF machine disagree')
                native[name] = data
                result.append({'path': name, 'sha256': sha256(data), 'loadSegments': segments})
            if not result:
                raise ValueError('No native libraries found')
            return result

        check('elfAlignmentAndAbi', libraries)

        def assets():
            info = archive.getinfo('assets/oro-build-evidence.json')
            if info.file_size > 16777216:
                raise ValueError('Asset manifest exceeds 16 MiB')
            data = archive.read(info)
            manifest = json.loads(data)
            if manifest['schema'] != 1 or manifest['androidBridgeProtocol'] != 5:
                raise ValueError('Unsupported native/JS contract')
            entries = manifest['assets']
            if len(entries) > 100000:
                raise ValueError('Asset count exceeds audit bound')
            declared = set()
            for entry in entries:
                path = 'assets/' + entry['path']
                if path in declared or any(part in ('.', '..', '') for part in entry['path'].split('/')):
                    raise ValueError('Duplicate or unsafe asset path')
                declared.add(path)
                item = archive.getinfo(path)
                if item.file_size != entry['size']:
                    raise ValueError('Packaged asset size mismatch')
                digest = hashlib.sha256()
                with archive.open(item) as stream:
                    for chunk in iter(lambda: stream.read(65536), b''):
                        digest.update(chunk)
                if digest.hexdigest() != entry['sha256']:
                    raise ValueError('Packaged asset digest mismatch')
            actual = {name for name in names if name.startswith('assets/') and not name.endswith('/')}
            generated_profiles = {}
            for path in sorted((actual - declared) & {
                    'assets/dexopt/baseline.prof', 'assets/dexopt/baseline.profm'}):
                item = archive.getinfo(path)
                if item.file_size > 16777216:
                    raise ValueError('Generated ART profile exceeds 16 MiB')
                generated_profiles[path] = sha256(archive.read(item))
            if actual != declared | {'assets/oro-build-evidence.json'} | set(generated_profiles):
                raise ValueError('Asset manifest and packaged asset inventory disagree')
            contract = json.loads(archive.read('assets/oro/internal/runtime-contract.json'))
            if contract['androidBridgeProtocol'] != 5:
                raise ValueError('Packaged JS bridge contract mismatch')
            digest = sha256(data)
            runtimes = [value for name, value in native.items() if name.endswith('/liboro-runtime.so')]
            if not runtimes or any(digest.encode() not in value for value in runtimes):
                raise ValueError('Native runtime is not bound to this asset manifest')
            archives = native_archive_evidence(manifest, {
                name.split('/')[1] for name in native if name.endswith('/liboro-runtime.so')})
            application_inputs = application_input_evidence(manifest)
            if manifest['inputs']['debug']:
                raise ValueError('Packaged evidence identifies a debug build')
            return {'manifestSha256': digest, 'files': len(entries), 'inputs': manifest['inputs'],
                    'nativeArchives': archives,
                    'applicationInputs': application_inputs,
                    'generatedArtProfiles': generated_profiles}

        check('packagedAssetsAndNativeBinding', assets)

    report['passed'] = all(check['passed'] for check in checks)
    output = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end='')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f'Android artifact verification failed: {error}', file=sys.stderr)
        sys.exit(1)
