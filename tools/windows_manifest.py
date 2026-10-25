from pathlib import Path
root=Path(__file__).resolve().parents[1]
stage=root/'dist/windows'
files=sorted(p for p in stage.rglob('*') if p.is_file())
dirs=sorted((p for p in stage.rglob('*') if p.is_dir()),key=lambda p:len(p.parts),reverse=True)
def esc(p):return str(p.relative_to(stage)).replace('/','\\').replace('$','$$').replace('"','$\\"')
lines=['; Remove only files shipped by this installer. Preserve user-created files.']
lines += ['Delete "$INSTDIR\\'+esc(p)+'"' for p in files]
lines += ['RMDir "$INSTDIR\\'+esc(p)+'"' for p in dirs]
(root/'packaging/uninstall-files.nsh').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'Generated exact-file uninstall manifest for {len(files)} files')
