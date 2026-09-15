"""Compare the full EDA PCB UI source fingerprint with the independent circuit model."""
from pathlib import Path
import json,csv
BASE=Path(__file__).resolve().parent
cfg=json.loads((BASE/'config.json').read_text(encoding='utf8'))
out=(BASE/cfg['output_dir']).resolve()
model=json.loads((out/'circuit-model.json').read_text(encoding='utf8'))
observed={'project_url':'https://pro.lceda.cn/editor','pcb_uuid':'46f0b7bbc17cfdb8','U1':1,'U1_pads':256,'canonical_pin_net_fnv1a32':'8be8e2ee','components':153,'intentional_open_pads':179,'named_nets':100,'pad_net_count':726,'source_length':428581,'trace_count':0,'source':'Read-only parse of the complete PCB File Source textarea in the live EDA UI, 2026-09-11. Hash input: sorted ref<TAB>pin<TAB>net lines, LF joined, no final LF. Null/open net encoded empty. FNV-1a32 is an accidental-difference check, not a cryptographic signature.'}
lines=sorted('\t'.join([p['ref'],str(p['pin']),p['net'] or '']) for p in model['pins'])
h=2166136261
for b in '\n'.join(lines).encode('ascii'):h=((h^b)*16777619)&0xffffffff
assert f'{h:08x}'==observed['canonical_pin_net_fnv1a32'],(f'{h:08x}',observed)
assert len(model['pins'])==observed['pad_net_count']
assert len({c['ref'] for c in model['components']})==observed['components']
observed['model_comparison']='PASS: entire canonical pin/net fingerprint and component count match local model'
observed['pcb_release']='Unrouted PCB preparation only. Default two-layer document/rules are placeholders; no board outline, stackup/fanout approval, routing or Gerber release.'
(out/'validation-pcb-transfer.json').write_text(json.dumps(observed,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(observed,ensure_ascii=False,indent=2))
