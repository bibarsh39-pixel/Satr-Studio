"""Build the embedded UI catalog; never translate source code or file names."""
from pathlib import Path
import json
r=Path(__file__).resolve().parents[1]
source=json.loads((r/'locale/source-messages.json').read_text(encoding='utf-8'))
pairs=(r/'locale/translation-pairs.tsv').read_text(encoding='utf-8').splitlines()
assert len(source)==len(pairs),(len(source),len(pairs))
catalog={}
for key,pair in zip(source,pairs):
    en,fr=pair.split('\t')
    catalog[key]=[en.replace('\\n','\n').replace('\\t','\t'),fr.replace('\\n','\n').replace('\\t','\t')]
extra=r/'locale/extra-messages.json'
if extra.exists():catalog.update(json.loads(extra.read_text(encoding='utf-8')))
(r/'locale/messages.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2),encoding='utf-8')
quote=lambda s:json.dumps(s,ensure_ascii=False)
lines=['// Generated from locale/messages.json. UTF-8; code and paths never pass through this catalog.']
for key,value in catalog.items():lines.append('{'+','.join(quote(x) for x in [key,*value])+'},')
(r/'src/ui_messages.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Embedded',len(catalog),'Arabic / English / French messages')
