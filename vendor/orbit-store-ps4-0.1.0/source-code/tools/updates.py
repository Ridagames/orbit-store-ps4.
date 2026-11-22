"""Compile PS4 game updates (patches) for the v8 catalogue's `updates` array.

An update record names its base game and the base game's title ID, the update
version and the oldest firmware it runs on. Each provider file keeps its own
source evidence. Feed rows reuse the base game's display fields and are never
shown as game options; older apps ignore the `updates` array.
"""
import copy
import re

ID = re.compile(r'update-cusa\d{5}-[a-z0-9-]{1,40}')
VERSION = re.compile(r'\d{1,2}\.\d{2}')
FIELDS = {'id', 'gameId', 'titleId', 'platform', 'version', 'requiredFirmware', 'backport', 'addedAt', 'releases'}
# Local research notes stay out of the catalogue files.
PRIVATE_EVIDENCE = ('researchPath',)


def strip_private(record):
    for option in record.get('releases', []):
        proof = option.get('importEvidence')
        if isinstance(proof, dict):
            for key in PRIVATE_EVIDENCE:
                proof.pop(key, None)
    return record


def compile_updates(games, base_releases, updates, addon_rows=()):
    from catalog import compile_catalog
    if not isinstance(updates, list) or len(updates) > 8192:
        raise ValueError('Expected an update record list')
    by_id = {g['id']: g for g in games}
    base_tids = {}
    for r in base_releases:
        base_tids.setdefault(r['gameId'], set()).add(r['titleId'])
    seen_ids = set(by_id) | {row['gameId'] for row in addon_rows}
    seen_releases = {r['id'] for r in base_releases} | {row['id'] for row in addon_rows}
    seen_urls = {r['url'] for r in base_releases} | {row['url'] for row in addon_rows}
    builds = set()
    rows = []
    for record in updates:
        if not isinstance(record, dict) or set(record) - FIELDS:
            raise ValueError('Unexpected update record field')
        uid = record.get('id')
        if not isinstance(uid, str) or not ID.fullmatch(uid) or len(uid.encode()) >= 64 or uid in seen_ids:
            raise ValueError('Invalid or duplicate update identity')
        seen_ids.add(uid)
        base = by_id.get(record.get('gameId'))
        if not base or base.get('platform', 'ps4') != 'ps4':
            raise ValueError('An update must refer to an existing PS4 base game')
        tid = record.get('titleId')
        if not isinstance(tid, str) or tid not in base_tids.get(base['id'], set()) or not tid.startswith('CUSA'):
            raise ValueError("An update's title ID must be one of its base game's PS4 title IDs")
        if uid[7:16] != tid.lower():
            raise ValueError("An update identity must start with its title ID")
        if record.get('platform') != 'ps4':
            raise ValueError('Updates are PS4 only')
        version, firmware = record.get('version'), record.get('requiredFirmware')
        if (not isinstance(version, str) or not VERSION.fullmatch(version) or version == '1.00' or
                not isinstance(firmware, str) or not VERSION.fullmatch(firmware)):
            raise ValueError('An update needs a version above 1.00 and an N.NN firmware')
        if type(record.get('backport')) is not bool:
            raise ValueError('An update must say whether it is a backport')
        build = (tid, version, firmware)
        if build in builds:
            raise ValueError('One record per title ID, version and firmware')
        builds.add(build)
        options = record.get('releases')
        if not isinstance(options, list) or not options or any(not isinstance(r, dict) for r in options):
            raise ValueError('Every update needs a download option')
        display = {k: copy.deepcopy(base.get(k)) for k in
                   ('title', 'genre', 'cover', 'hero', 'coverFallback', 'heroFallback', 'artworkLayout', 'publisher')}
        display.update(id=uid, platform='ps4', description=base.get('description'), tagline=None,
                       releaseDate=None, addedAt=record.get('addedAt'))
        for option in options:
            if option.get('id') in seen_releases or option.get('url') in seen_urls:
                raise ValueError('Duplicate update/base-game download identity or URL')
            seen_releases.add(option.get('id')); seen_urls.add(option.get('url'))
            if any(k in option for k in ('addonId', 'baseGameId', 'addonType', 'contentId', 'requirements',
                                         'updateId', 'updateVersion', 'requiredFirmware', 'backport', 'compression')):
                raise ValueError('An update option carries only its own file details')
            if option.get('titleId', tid) != tid or option.get('format') not in ('PKG', 'FPKG'):
                raise ValueError('An update option must be a package for its record title ID')
            if not isinstance(option.get('filename'), str) or not option['filename'].lower().endswith('.pkg'):
                raise ValueError('An update option must be a .pkg file')
            proof = option.get('importEvidence')
            if not isinstance(proof, dict) or not proof.get('sourceIndex') or not proof.get('checks'):
                raise ValueError('An update needs source evidence for its game, version and firmware')
            selected = copy.deepcopy(option)
            selected.update(gameId=uid, titleId=tid, platform='ps4')
            # Shared filename/provider/evidence validation, exactly as base games.
            for row in compile_catalog([display], [selected], _addon=True):
                row.update(updateId=uid, baseGameId=base['id'], baseGameTitle=base['title'],
                           updateVersion=version, requiredFirmware=firmware, backport=record['backport'])
                rows.append(row)
    if len(rows) + len(addon_rows) + len(base_releases) > 8192:
        raise ValueError('Base-game, add-on and update options exceed the console capacity')
    return rows
