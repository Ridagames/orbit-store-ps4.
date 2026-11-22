"""Embed the built UI and curated catalogue; no filesystem web root on PS4."""
import base64, gzip, hashlib, json, mimetypes
from html.parser import HTMLParser
from pathlib import Path
from catalog_crypto import embedded_feed, unseal
root = Path(__file__).resolve().parent.parent
out = root / 'build/generated'
out.mkdir(parents=True, exist_ok=True)
parts = ['#ifndef ORBIT_ASSETS_H\n#define ORBIT_ASSETS_H\n#include <stddef.h>\n']
# Vite's legacy loader uses a few inline bootstrap scripts. Permit precisely
# the emitted bytes; do not weaken script-src with unsafe-inline or unsafe-eval.
class InlineScripts(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.inside = False
        self.current = ''
        self.scripts = []
    def handle_starttag(self, tag, attrs):
        if tag == 'script':
            self.inside = 'src' not in dict(attrs)
            self.current = ''
    def handle_data(self, data):
        if self.inside: self.current += data
    def handle_endtag(self, tag):
        if tag == 'script':
            if self.inside and self.current: self.scripts.append(self.current)
            self.inside = False
inline = InlineScripts()
inline.feed((root / 'dist/index.html').read_text())
hashes = sorted({"'sha256-" + base64.b64encode(hashlib.sha256(text.encode()).digest()).decode() + "'"
                 for text in inline.scripts})
parts.append('#define ORBIT_SCRIPT_CSP ' + json.dumps(' '.join(hashes)) + '\n')
rows = []
for i, p in enumerate(sorted((root / 'dist').rglob('*'))):
    if not p.is_file(): continue
    raw = p.read_bytes()
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    encoded = len(compressed) < len(raw)
    data = compressed if encoded else raw
    parts.append(f'static const unsigned char asset_{i}[] = {{{",".join(str(b) for b in data)}}};\n')
    mime = mimetypes.guess_type(p.name)[0] or 'application/octet-stream'
    rows.append('{'+json.dumps('/'+str(p.relative_to(root/'dist')))+','+json.dumps(mime)+f',asset_{i},sizeof(asset_{i}),{len(raw)},{int(encoded)}'+'}')
parts.append('struct orbit_asset { const char *path, *mime; const unsigned char *data; size_t size, raw_size; int gzip; };\n')
parts.append('static const struct orbit_asset assets[] = {'+','.join(rows)+'};\n#endif\n')
(out/'assets.h').write_text(''.join(parts))
# The feed compressed before sealing (catalog:build writes embedded.enc); the service inflates it
# to catalog_plain_size bytes at start.
catalog = (root/'catalog/embedded.enc').read_bytes()
plain = embedded_feed(catalog)
revision = json.loads((root/'catalog/revision.json').read_text())['revision']
if json.loads(plain)['revision'] != revision or plain != unseal((root/'catalog/catalog.enc').read_bytes()):
    raise ValueError('Embedded catalogue is stale; run catalog:build')
(out/'catalog.h').write_text('#include <stddef.h>\n' + f'static const unsigned catalog_revision = {revision};\n' +
    f'static const size_t catalog_plain_size = {len(plain)};\n' +
    'static const unsigned char catalog_data[] = {' + ','.join(str(b) for b in catalog) + '};\n')
ca = root/'.deps/cacert.pem'
if ca.exists():
    (out/'ca.h').write_text('static const char orbit_ca[] = '+json.dumps(ca.read_text())+';\n')
print(f'Embedded {len(rows)} UI assets and catalogue')
