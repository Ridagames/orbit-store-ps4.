// Get everything and Update: which update and add-ons go with a game option,
// the checklist's rows and totals, and the request that adds them to Downloads
// (POST /api/v1/bundles). Labels are left to each look.
import type { Bundle, BundleItem, Game, GameUpdate, Job, LibraryGame, Release, Storage } from "./types";
import { preferredRelease } from "./downloadChoice";
import { defaultDrive, downloadSpacePlan } from "./collection";
import { gamePlatform } from "./platform";

type Choice = { source: string; format: string } | null;

// "1.05" < "1.27" < "10.00"; firmware the same way ("6.72" < "9.00").
export function compareVersion(a: string, b: string) {
  const left = a.trim().split(".").map((n) => parseInt(n, 10) || 0);
  const right = b.trim().split(".").map((n) => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    const d = (left[i] || 0) - (right[i] || 0);
    if (d) return d < 0 ? -1 : 1;
  }
  return 0;
}

const sameTitle = (a: string, b: string) => a.toUpperCase() === b.toUpperCase();

// An update this console's firmware runs: every one when the firmware is unknown.
export function runsOn(row: Pick<GameUpdate, "requiredFirmware" | "firmwareIssue">, firmware?: string) {
  if (row.firmwareIssue === "newer-firmware") return false;
  if (!firmware || !row.requiredFirmware) return true;
  return compareVersion(row.requiredFirmware, firmware) <= 0;
}

// The newest update for a title ID that this console runs, as its provider
// options (one record, one or more sources). The highest version wins; for the
// same version the highest firmware build that still runs (native over backport).
// None when the installed version is already that new.
export function chooseUpdate(rows: GameUpdate[], titleId: string, firmware?: string, installedVersion?: string): GameUpdate[] {
  const records = new Map<string, GameUpdate[]>();
  for (const row of rows) {
    if (!sameTitle(row.titleId, titleId) || !runsOn(row, firmware)) continue;
    const id = row.updateId || row.gameId;
    records.set(id, [...(records.get(id) || []), row]);
  }
  let best: GameUpdate[] = [];
  for (const record of records.values()) {
    const [a, b] = [record[0], best[0]];
    if (!b || compareVersion(a.updateVersion, b.updateVersion) > 0 ||
        (compareVersion(a.updateVersion, b.updateVersion) === 0 &&
          compareVersion(a.requiredFirmware || "0", b.requiredFirmware || "0") > 0))
      best = record;
  }
  if (best.length && installedVersion && compareVersion(best[0].updateVersion, installedVersion) <= 0) return [];
  return best;
}

// The option a download starts on, as the Download button chooses it.
export function pickOption<T extends Release>(releases: T[], last?: Choice): T {
  return (last === undefined ? preferredRelease(releases) : preferredRelease(releases, undefined, last)) as T;
}

// The add-ons for a game option: those with options for the same CUSA.
export function addonOptions(addons: Game[], titleId: string) {
  return addons
    .map((addon) => ({
      addon,
      releases: addon.releases.filter((r) => sameTitle(r.titleId, titleId) && gamePlatform(r) === "ps4"),
    }))
    .filter((entry) => entry.releases.length > 0);
}

// The Library copy that counts as this game installed.
export function installedCopy(library: LibraryGame[], titleId: string) {
  return library.find((g) => sameTitle(g.titleId, titleId) && g.platform !== "ps5" && g.installed);
}

export interface ChecklistRow {
  key: string;
  kind: "game" | "update" | "dlc";
  release: Release;
  // The add-on's name (DLC rows).
  title: string;
  // The update's version (update row).
  version: string;
  // Game row only: Library has it installed, so it is left out.
  installed: boolean;
}

// Get everything's rows, in install order: the game option the Download button
// uses, the newest update this console runs (unless the installed copy is that
// new already), then each add-on for the same CUSA.
export function checklistRows({
  release,
  updates,
  addons,
  library,
  firmware,
  last,
}: {
  release: Release;
  updates: GameUpdate[];
  addons: Game[];
  library: LibraryGame[];
  firmware?: string;
  last?: Choice;
}): ChecklistRow[] {
  if (release.addonId || gamePlatform(release) !== "ps4") return [];
  const copy = installedCopy(library, release.titleId);
  const rows: ChecklistRow[] = [
    { key: "game", kind: "game", release, title: "", version: "", installed: !!copy },
  ];
  const record = chooseUpdate(updates, release.titleId, firmware, copy?.installedVersion || undefined);
  if (record.length)
    rows.push({ key: "update", kind: "update", release: pickOption(record, last), title: "", version: record[0].updateVersion, installed: false });
  for (const { addon, releases } of addonOptions(addons, release.titleId))
    rows.push({ key: `dlc:${addon.id}`, kind: "dlc", release: pickOption(releases, last), title: addon.title, version: "", installed: false });
  return rows;
}

// Get everything is offered when there is more than the game to get.
export const hasExtras = (rows: ChecklistRow[]) => rows.some((r) => r.kind !== "game");

// Ticked unless it is the installed game.
export const defaultTicks = (rows: ChecklistRow[]) =>
  new Set(rows.filter((r) => !(r.kind === "game" && r.installed)).map((r) => r.key));

export const chosenRows = (rows: ChecklistRow[], ticked: ReadonlySet<string>) =>
  rows.filter((r) => ticked.has(r.key) && !(r.kind === "game" && r.installed));

// What a set of downloads needs on a drive, as the Download screen counts it:
// free space after unfinished downloads, less each item not queued there yet.
// Room to unpack an archive is checked when it is unpacked.
export function spaceFor(releases: Release[], drive: Storage | undefined, jobs: Job[]) {
  const total = releases.reduce((n, r) => n + r.sizeBytes, 0);
  const free = drive ? drive.projectedFreeBytes : 0;
  const need = drive
    ? releases.reduce((n, r) => (downloadSpacePlan(r, drive, jobs).planned ? n : n + r.sizeBytes), 0)
    : 0;
  return { total, free, enough: !!drive && free - need >= 16 * 1024 ** 2 };
}

// The drive a set of downloads goes to: Download's own rule, for one item or
// for all of them at once.
export function driveFor(drives: Storage[], preferred: string | undefined, releases: Release[], jobs: Job[]) {
  if (!releases.length) return undefined;
  if (releases.length === 1) return defaultDrive(drives, preferred, releases[0], jobs);
  const all: Release = { ...releases[0], id: "", sizeBytes: releases.reduce((n, r) => n + r.sizeBytes, 0) };
  return defaultDrive(drives, preferred, all, jobs);
}

// The console adds 1 to 64 items in one Download all.
export const BUNDLE_LIMIT = 64;

export function bundleRequest(
  storageId: string,
  items: { release: Release; delivery?: "direct" | "torbox" }[],
  installInOrder: boolean,
  returnToOrbit = true,
) {
  return {
    action: "start",
    storageId,
    items: items.map((i) => ({ releaseId: i.release.id, ...(i.delivery ? { delivery: i.delivery } : {}) })),
    installInOrder,
    ...(returnToOrbit ? { returnToOrbit: true } : {}),
  };
}

export const bundleRunning = (b: Bundle | null | undefined) => !!b && (b.state === "running" || b.state === "page");

// A row's place in a Download all: added, its page open on the PS4, waiting
// its turn (adding: being added now), or not added (failed or skipped).
export type RowProgress = "added" | "page" | "adding" | "waiting" | "not-added";
export function rowProgress(item: Pick<BundleItem, "state"> | undefined): RowProgress {
  switch (item?.state) {
    case "added":
      return "added";
    case "page":
      return "page";
    case "adding":
      return "adding";
    case "failed":
    case "skipped":
      return "not-added";
    default:
      return "waiting";
  }
}

// The bundle these rows started (or one running with any of them).
export function bundleFor(bundle: Bundle | null, releaseIds: string[], startedId = "") {
  if (!bundle || !bundle.items.length) return null;
  if (startedId && bundle.id === startedId) return bundle;
  return bundleRunning(bundle) && bundle.items.some((i) => releaseIds.includes(i.releaseId)) ? bundle : null;
}

export const addedCount = (bundle: Bundle) => bundle.items.filter((i) => i.state === "added").length;

// Library: the update Library reports for a game, with the option to download.
export interface UpdatePlan {
  game: LibraryGame;
  release: GameUpdate;
  version: string;
  from: string;
}
export function libraryUpdate(game: LibraryGame, updates: GameUpdate[], last?: Choice): UpdatePlan | null {
  if (!game.update) return null;
  const record = updates.filter((r) => (r.updateId || r.gameId) === game.update!.gameId);
  if (!record.length) return null;
  return { game, release: pickOption(record, last), version: game.update.version, from: game.installedVersion || "" };
}
