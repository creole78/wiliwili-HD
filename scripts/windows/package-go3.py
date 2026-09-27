"""Collect the actual recursive PE imports; do not require MSYS2 on the target PC."""
from pathlib import Path
import sys, subprocess, re, shutil, json, hashlib, zipfile
repo, prefix = map(Path, sys.argv[1:3])
if 'GO3_PLAYER_REGRESSION:BOOL=ON' in (repo/'build-go3/CMakeCache.txt').read_text(encoding='utf-8'):
    raise SystemExit('Refusing to package an integration-test executable; rebuild with GO3_PLAYER_REGRESSION=OFF')
output = repo / 'dist' / 'wiliwili-Go3-Windows-x64'
output.mkdir(parents=True, exist_ok=True)
exe = repo / 'build-go3/wiliwili.exe'
search = [prefix/'bin', repo/'build-go3/library/cpr/cpr']
lookup = {p.name.lower():p for folder in search for p in folder.glob('*.dll')}
system = Path('C:/Windows/System32')
pending=[exe]; copied={}; missing=set()
while pending:
    source = pending.pop()
    if source.name.lower() in copied: continue
    target = output/source.name
    shutil.copy2(source,target)
    copied[source.name.lower()] = hashlib.sha256(target.read_bytes()).hexdigest()
    imports = subprocess.check_output([str(prefix/'bin/objdump.exe'),'-p',str(source)],text=True,encoding='utf-8',errors='replace')
    for name in re.findall(r'DLL Name:\s*(\S+)',imports):
        if name.lower() in lookup: pending.append(lookup[name.lower()])
        elif not (system/name).exists() and not name.lower().startswith(('api-ms-','ext-ms-')): missing.add(name)
if missing: raise SystemExit('Unresolved DLLs: '+', '.join(sorted(missing)))
shutil.copy2(repo/'LICENSE',output/'LICENSE')
shutil.copy2(repo/'docs/GO3-WINDOWS.md',output/'README-Go3.md')
shutil.copy2(repo/'docs/go3-source-manifest.json',output/'source-manifest.json')
shutil.copy2(repo/'docs/GO3-VALIDATION.md',output/'GO3-VALIDATION.md')
# Include dependency license texts distributed by MSYS2.
licenses=output/'licenses'; licenses.mkdir(exist_ok=True)
for folder in (prefix/'share/licenses').iterdir():
    if folder.is_dir(): shutil.copytree(folder,licenses/folder.name,dirs_exist_ok=True)
(output/'SHA256.json').write_text(json.dumps(copied,indent=2),encoding='utf-8')
archive=output.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in output.rglob('*'):
        if f.is_file(): z.write(f,f.relative_to(output.parent))
print(f'Packaged {len(copied)} executable/DLL files: {archive}')
