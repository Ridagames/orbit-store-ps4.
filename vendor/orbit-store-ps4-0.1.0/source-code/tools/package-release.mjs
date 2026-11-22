// Stage both programs, their exact source and pinned dependency sources. Never uploads.
import { execFileSync } from 'node:child_process';
import { copyFileSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { checkRelease } from './release-check.mjs';
import { sha256, validateArtifacts } from './release-artifacts.mjs';
const root = new URL('../', import.meta.url);
const path = relative => fileURLToPath(new URL(relative, root));
const git = (...args) => execFileSync('git', args, { cwd: path('.'), encoding: 'utf8' }).trim();
const json = relative => JSON.parse(readFileSync(path(relative), 'utf8'));
const status = json('release-status.json');
const blockers = checkRelease(status);
const review = process.argv.includes('--review');
if (process.argv.slice(2).some(arg => arg !== '--review')) throw new Error('Only --review is supported.');
if (blockers.length && !review) throw new Error(`Cannot package a public release: ${blockers.join(', ')}`);
if (git('status', '--porcelain')) throw new Error('Commit or stash every change first: the source archive must match the binaries.');
// The exported source uses encrypted inputs. Fail before staging anything if
// those inputs do not match the reviewed catalogue or bundled revision.
execFileSync('python3', ['tools/catalog.py', 'check'], {cwd: path('.'), stdio: 'inherit'});
const versionNotes = readFileSync(path(`docs/${status.version}-release-notes.md`), 'utf8')
  .replace(/^# [^\n]+\n\s*/, '').trim();
if (!versionNotes || /Draft release notes|not published yet/i.test(versionNotes))
  throw new Error('Finalize the versioned release notes before packaging.');
const commit = git('rev-parse', 'HEAD');
const artifact = path('build/orbit_store.elf');
const tvArtifact = path(`app/dist/${json('app/sce_sys/param.json').contentId}.pkg`);
const payload = readFileSync(artifact), image = readFileSync(tvArtifact);
const verification = json('app/build/package-verification.json');
const tvFeed = validateArtifacts({status, packageVersion:json('package.json').version,
  header:readFileSync(path('backend/orbit.h'),'utf8'), param:json('app/sce_sys/param.json'),
  commit, payload, image, verification});
for (const file of [artifact, tvArtifact]) {
  if (statSync(file).mtimeMs < Number(git('log', '-1', '--format=%ct')) * 1000)
    throw new Error(`${file} is older than the last commit. Rebuild the release first.`);
}
const out = path(`build/${review ? 'review' : 'release'}-${status.version}/`);
rmSync(out, {recursive:true, force:true});
execFileSync('python3', ['tools/source_bundle.py', '--output', out, '--fetch',
  ...(review ? ['--review'] : [])], {cwd:path('.'), stdio:'inherit'});
const sourceBundle = `orbit-store-${status.version}-source-bundle.zip`;
if (!versionNotes.includes(sourceBundle)) throw new Error(`The release notes must name ${sourceBundle}.`);
function filesIn(directory, prefix='') {
  return readdirSync(directory, {withFileTypes:true}).flatMap(entry => entry.isDirectory()
    ? filesIn(`${directory}/${entry.name}`,`${prefix}${entry.name}/`) : [`${prefix}${entry.name}`]).sort();
}
function checksums(names) {
  return names.map(name => `${sha256(readFileSync(`${out}${name}`))}  ${name}\n`).join('');
}
copyFileSync(artifact, `${out}orbit_store.elf`);
copyFileSync(tvArtifact, `${out}orbit_store_ps4.pkg`);
writeFileSync(`${out}orbit_store.elf.sha256`, `${sha256(payload)}  orbit_store.elf\n`);
writeFileSync(`${out}orbit_store_ps4.pkg.sha256`, `${sha256(image)}  orbit_store_ps4.pkg\n`);
writeFileSync(`${out}tv-app.json`, JSON.stringify(tvFeed,null,2)+'\n');
const payloadFeed = {name:'Orbit Store',payloads:[{
  name:'Orbit Store (Beta)',filename:'orbit_store.elf',
  url:`https://github.com/saawant12/orbit-store-ps4/releases/download/v${status.version}/orbit_store.elf`,
  description:'A modern PS4 download manager with a native TV app, resumable downloads and phone controls.',
  version:`v${status.version}`,category:'Utilities',checksum:sha256(payload),receiveProtocol:1,
}]};
writeFileSync(`${out}payloads.json`,JSON.stringify(payloadFeed,null,2)+'\n');
writeFileSync(`${out}BUILD-INFO.json`,JSON.stringify({sourceCommit:commit,version:status.version,
  tvAppVersion:status.tvAppVersion,packageVerification:verification,
  binaries:[{name:'orbit_store.elf',size:payload.length,sha256:sha256(payload)},
    {name:'orbit_store_ps4.pkg',size:image.length,sha256:sha256(image)}]},null,2)+'\n');
// The versioned notes are the release text as published (install steps, downloads and
// support button included); only the title and a review banner are added here.
writeFileSync(`${out}RELEASE-NOTES.md`, `# Orbit Store ${status.version} (Beta)

${review ? `UNRELEASED REVIEW BUNDLE. Outstanding gates: ${blockers.join(', ') || 'none'}. Do not publish this review folder.\n\n` : ''}${versionNotes}
`);
writeFileSync(`${out}SHA256SUMS`,checksums(filesIn(out)));
console.log(`Staged ${out} from ${commit}. Nothing was uploaded. Publish the two binaries, their checksums, the source bundle and SHA256SUMS together; update both feeds only after verifying the release assets.`);
