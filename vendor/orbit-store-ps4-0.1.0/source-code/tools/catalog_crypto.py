"""Versioned AES-256-GCM envelopes via Orbit's shared OpenSSL implementation.

No Python crypto implementation or extra Python package is needed. The build
machine needs a C compiler and OpenSSL development files (brew install openssl@3
on macOS). The distributed key intentionally provides scraping resistance only.
"""
import json
import zlib
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
FEED, GAMES, RELEASES, ADDONS, UPDATES = range(1, 6)
# The payload's own copy of the feed: zlib-compressed, then sealed (embedded.enc, tools/embed.py).
EMBEDDED = 6
# Compatibility generation, independent of the envelope encoding and revision.
FEED_VERSION = 8
LIMIT, OVERHEAD = 16 * 1024 * 1024, 40


def helper():
    inputs = [ROOT / name for name in ('backend/catalog_crypto.c', 'backend/catalog_crypto.h',
                                      'backend/catalog_key.h', 'tools/catalog_crypto_cli.c',
                                      'tools/catalog_crypto.py')]
    target = ROOT / 'build/tools' / f'catalog-crypto-{platform.system()}-{platform.machine()}'
    if target.exists() and target.stat().st_mtime_ns >= max(p.stat().st_mtime_ns for p in inputs):
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'openssl'],
                                                    text=True, stderr=subprocess.DEVNULL))
    except (FileNotFoundError, subprocess.CalledProcessError):
        flags = ['-lcrypto']
        if platform.system() == 'Darwin':
            for prefix in ('/opt/homebrew/opt/openssl@3', '/usr/local/opt/openssl@3'):
                if (Path(prefix) / 'include/openssl/evp.h').exists():
                    flags = [f'-I{prefix}/include', f'-L{prefix}/lib', '-lcrypto']
                    break
    fd, temporary = tempfile.mkstemp(prefix=target.name, dir=target.parent)
    os.close(fd)
    try:
        compiler = os.environ.get('CC') or next((name for name in
                    ('cc', 'clang', 'clang-19', 'clang-18', 'gcc') if shutil.which(name)), None)
        if not compiler:
            raise RuntimeError('Catalogue tools require a host C compiler and OpenSSL development files')
        subprocess.run([*shlex.split(compiler), '-std=c11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'backend'),
                        str(inputs[0]), str(inputs[3]), '-o', temporary, *flags], check=True)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return target


def transform(operation, data, purpose):
    if not isinstance(data, bytes) or not data or len(data) > LIMIT + OVERHEAD:
        raise ValueError('Invalid catalogue envelope size')
    result = subprocess.run([str(helper()), operation, str(purpose)], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError('Invalid catalogue envelope or encryption failure')
    return result.stdout


def seal(data, purpose=FEED):
    return transform('seal', data, purpose)


def unseal(data, purpose=FEED):
    return transform('open', data, purpose)


def encode(value):
    return (json.dumps(value, ensure_ascii=False, separators=(',', ':')) + '\n').encode()


def sealed_bytes(value, purpose, existing=None):
    """Reuse authenticated unchanged bytes, avoiding nonce churn on rebuilds."""
    plain = encode(value)
    if existing is not None and existing.exists():
        current = existing.read_bytes()
        if unseal(current, purpose) == plain:
            return current
    return seal(plain, purpose)


def embedded_bytes(feed, existing=None):
    """The feed as the payload carries it: compressed before sealing, since stored plain it took
    11.6 MB twice over in orbit_store.elf. zlib output is the same for the same input, and unchanged
    bytes are reused, as in sealed_bytes."""
    plain = encode(feed)
    if existing is not None and existing.exists():
        current = existing.read_bytes()
        if zlib.decompress(unseal(current, EMBEDDED)) == plain:
            return current
    return seal(zlib.compress(plain, 9), EMBEDDED)


def embedded_feed(data):
    """The plain feed bytes of embedded.enc."""
    return zlib.decompress(unseal(data, EMBEDDED))


def feed_envelope(revision, rows, addon_rows=None, update_rows=None):
    """The v8 feed: older apps read only `releases` and `addons` and ignore `updates`."""
    return {'schemaVersion': 1, 'feedVersion': FEED_VERSION, 'revision': revision,
            'releases': rows, 'addons': addon_rows or [], 'updates': update_rows or []}


def source_envelopes(directory, games, releases, rows, revision, addons=None, addon_rows=None,
                     updates=None, update_rows=None):
    values = [('games.enc', games, GAMES), ('releases.enc', releases, RELEASES),
              ('addons.enc', addons or [], ADDONS), ('updates.enc', updates or [], UPDATES),
              ('catalog.enc', feed_envelope(revision, rows, addon_rows, update_rows), FEED)]
    out = [(directory / name, sealed_bytes(value, purpose, directory / name))
           for name, value, purpose in values]
    feed = feed_envelope(revision, rows, addon_rows, update_rows)
    return out + [(directory / 'embedded.enc', embedded_bytes(feed, directory / 'embedded.enc'))]
