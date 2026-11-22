import { createHash } from 'node:crypto';

export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
export function validateArtifacts({ status, packageVersion, header, param, commit, payload, image, verification }) {
  const fail = message => { throw new Error(message); };
  if (status.version !== packageVersion || !header.includes(`#define ORBIT_VERSION "${status.version}"`))
    fail('Release, npm and payload versions do not match');
  if (!/^\d{2}\.\d{3}\.\d{3}$/.test(param.contentVersion || '')) fail('Invalid TV content version');
  if (!/^[0-9]{2}\.[0-9]{2}$/.test(param.appVersion || '')) fail('Invalid PS4 application version');
  const tvVersion = param.contentVersion.split('.').map(Number).join('.');
  if (tvVersion !== status.tvAppVersion || param.titleId !== 'ORBT00002') fail('TV app release metadata does not match');
  if (payload.subarray(0, 4).toString('hex') !== '7f454c46') fail('Invalid ELF artifact');
  if (!payload.includes(Buffer.from(`Orbit-Store/${status.version}\0`))) fail('ELF has the wrong version');
  if (image.length < 4096 || image.length > 256 * 1024 * 1024 || image.readUInt32BE(0) !== 0x7f434e54)
    fail('Invalid or oversized PKG artifact');
  if (verification.sourceCommit !== commit || verification.payloadVersion !== status.version ||
      verification.contentVersion !== param.contentVersion || verification.appVersion !== param.appVersion || verification.titleId !== param.titleId ||
      verification.payloadSha256 !== sha256(payload) || verification.imageSha256 !== sha256(image))
    fail('Package verification is stale. Run app/tools/verify-package.sh in the app build image.');
  if (verification.shaderAssetsVerified !== true)
    fail('The PS4 package has no verified Piglet shader assets. Complete shader capture before release.');
  return {
    name: 'Orbit Store for TV', filename: 'orbit_store_ps4.pkg', version: tvVersion,
    url: `https://github.com/saawant12/orbit-store-ps4/releases/download/v${status.version}/orbit_store_ps4.pkg`,
    checksum: sha256(image), size: image.length,
    // Verified above to be the service inside this package: updating both parts downloads once.
    serviceVersion: status.version,
  };
}
