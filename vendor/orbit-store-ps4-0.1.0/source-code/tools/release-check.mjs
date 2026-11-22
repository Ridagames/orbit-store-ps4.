import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
// Gates the owner chose to release without, for one exact beta version, each with the
// owner's own reason. Only these two can be deferred; tests, documented limitations and
// release preparation never can.
const DEFERRABLE = ['licenseCleared', 'catalogLinksVerified'];
function deferredGates(s) {
  const d = s.ownerDeferredGates;
  if (d?.version !== s.version || d?.authorizedByOwner !== true || !d.gates || typeof d.gates !== 'object') return [];
  return DEFERRABLE.filter(gate => typeof d.gates[gate] === 'string' && d.gates[gate].trim().length > 0);
}
export function checkRelease(s) {
  const preparation = s.platform === 'PS4'
    ? ['idsFinalized', 'sourceBundleReady', 'catalogueMetadataChecked', 'noticesUpdated', 'releaseNotesReady']
      .filter(key => s.releasePreparation?.[key] !== true).map(key => `releasePreparation.${key}`)
    : [];
  if (s.channel === 'beta' && /^\d+\.\d+\.\d+(?:-beta\.\d+)?$/.test(s.version || '')) {
    const deferred = deferredGates(s);
    const blockers = ['licenseCleared', 'localRegressionPassed', 'catalogLinksVerified', 'betaLimitationsDocumented']
      .filter(k => s[k] !== true && !deferred.includes(k));
    const deferral = s.betaConsoleCheckDeferred;
    const ownerDeferred = deferral?.version === s.version && deferral?.authorizedByOwner === true &&
      typeof deferral?.reason === 'string' && deferral.reason.trim().length > 0;
    if (s.betaConsoleSmokePassed !== true && !ownerDeferred) blockers.push('betaConsoleSmokePassed');
    return [...blockers, ...preparation];
  }
  return [...['licenseCleared', 'consoleTestPassed', 'catalogSourcesVerifiedOnConsole', 'homeScreenLifecycleVerified'].filter(k => s[k] !== true), ...preparation];
}
if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const state = JSON.parse(readFileSync(new URL('../release-status.json', import.meta.url), 'utf8'));
  const blockers = checkRelease(state);
  if (blockers.length) { console.error(`Public payload release blocked: ${blockers.join(', ')}.\n${state.reason}`); process.exit(1); }
  console.log('Release evidence gates passed.');
}
