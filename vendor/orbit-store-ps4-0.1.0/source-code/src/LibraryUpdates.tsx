// Library's updates: the badge on games with a newer update, the Updates
// filter, Update all and each game's Update. Every update is added as a
// Download all of its own (POST /api/v1/bundles), so it installs by itself.
import { useMemo, useState } from "react";
import { bytes, type Job, type LibraryGame, type Storage, type System } from "./types";
import { BUNDLE_LIMIT, bundleRequest, bundleRunning, driveFor, libraryUpdate, type UpdatePlan } from "./bundle";
import { releaseState } from "./collection";
import { preferredDelivery, useDebrid } from "./Debrid";
import { t, tn } from "./i18n";
import type { BundleCatalog } from "./GetEverything";
import { Glyph } from "./interfaces/Glyph";

// "Installed 1.05" when Library knows the version.
export const installedText = (g: LibraryGame) =>
  g.installedVersion ? t("Installed {version}", { version: g.installedVersion }) : t("Installed");

// A Library game's status, as every look's status line says it.
export const libraryStatus = (g: LibraryGame) =>
  !g.onDrive ? t("Unavailable") : g.mounted ? t("Mounted") : g.installed ? installedText(g) : t("On drive");

export function useLibraryUpdates({
  games,
  catalog,
  drives,
  jobs,
  system,
  paired,
}: {
  games: LibraryGame[];
  catalog: BundleCatalog;
  drives: Storage[];
  jobs: Job[];
  system: System | null;
  paired: boolean;
}) {
  const withUpdate = useMemo(() => games.filter((g) => g.update), [games]);
  const plans = useMemo(
    () =>
      new Map(
        withUpdate
          .map((g) => [g.titleId, libraryUpdate(g, catalog.updates)] as const)
          .filter((entry): entry is readonly [string, UpdatePlan] => !!entry[1]),
      ),
    [withUpdate, catalog.updates],
  );
  const debrid = useDebrid(paired && withUpdate.length > 0);
  const [pending, setPending] = useState("");
  const [error, setError] = useState<{ key: string; message: string } | null>(null);
  const [started, setStarted] = useState(""); // the Download all Update all started here
  const running = bundleRunning(catalog.bundle);
  // An update already on its way: its download's state, or its turn in a Download all.
  function progress(plan: UpdatePlan) {
    const item = running ? catalog.bundle!.items.find((i) => i.releaseId === plan.release.id) : undefined;
    if (item?.state === "page") return t("Open its page on your PS4");
    if (item && (item.state === "pending" || item.state === "adding")) return t("Waiting");
    const state = releaseState(plan.release, jobs, []);
    if (state?.job && state.job.status !== "error") return t(state.label);
    return "";
  }
  // Why the last Download all didn't add an update ("Not added · …"), until it's on its way again.
  function refused(plan: UpdatePlan) {
    if (running || progress(plan)) return "";
    const item = catalog.bundle?.items.find((i) => i.releaseId === plan.release.id);
    if (!item || (item.state !== "failed" && item.state !== "skipped")) return "";
    return item.message ? `${t("Not added")} · ${t(item.message)}` : t("Not added");
  }
  const open = [...plans.values()].filter((plan) => !progress(plan));
  async function run(list: UpdatePlan[], key: string) {
    if (!list.length) return;
    setPending(key);
    setError(null);
    try {
      const releases = list.map((p) => p.release);
      const drive = driveFor(drives, system?.preferredStorage, releases, jobs);
      if (!drive) throw new Error(t("Connect a writable drive to your PS4."));
      const status = await catalog.startBundle(
        bundleRequest(drive.id, list.map((p) => ({ release: p.release, delivery: preferredDelivery(debrid.status, p.release) })), false),
      );
      if (key === "all") setStarted(status?.id || "");
    } catch (e) {
      setError({ key, message: (e as Error).message });
    } finally {
      setPending("");
    }
  }
  const allSize = open.reduce((n, p) => n + p.release.sizeBytes, 0);
  return {
    // Games Library reports an update for (the Updates filter's count).
    count: system?.paired ? withUpdate.length : 0,
    plan: (g: LibraryGame) => plans.get(g.titleId) || null,
    progress,
    // The game's Update button: "Update · 12.1 GB", or where that update is.
    label: (plan: UpdatePlan) => progress(plan) || `${t("Update")} · ${bytes(plan.release.sizeBytes)}`,
    busy: (plan: UpdatePlan) => !!pending || running || !!progress(plan),
    update: (plan: UpdatePlan) => run([plan], plan.game.titleId),
    // Update all: the games whose update isn't on its way yet.
    allLabel: `${t("Update all")} · ${bytes(allSize)}`,
    allSummary: `${tn(open.length, "{count} game", "{count} games")} · ${bytes(allSize)}`,
    allDisabled: !open.length || !!pending || running,
    // The console takes 64 at a time; Update all again adds the rest.
    updateAll: () => run(open.slice(0, BUNDLE_LIMIT), "all"),
    pending,
    // A start that failed, or why the PS4 didn't add an update: for one game, from the last
    // Download all; for Update all, from the one it started.
    error: (key: string) => {
      if (error?.key === key) return error.message;
      if (key !== "all") {
        const plan = [...plans.values()].find((p) => p.game.titleId === key);
        return plan ? refused(plan) : "";
      }
      if (!started || catalog.bundle?.id !== started) return "";
      const plan = [...plans.values()].find((p) => refused(p));
      return plan ? `${plan.game.title} · ${refused(plan)}` : "";
    },
  };
}
export type LibraryUpdates = ReturnType<typeof useLibraryUpdates>;

// Each update's facts: source, size and the firmware it runs on.
export const planFacts = (plan: UpdatePlan) =>
  [
    plan.release.provider,
    bytes(plan.release.sizeBytes),
    plan.release.requiredFirmware ? t("Runs on {firmware} and newer", { firmware: plan.release.requiredFirmware }) : "",
  ]
    .filter(Boolean)
    .join(" · ");

// "1.05 → 1.27", or the new version alone when the installed one is unknown.
export const versionStep = (plan: UpdatePlan) => (plan.from ? `${plan.from} → ${plan.version}` : plan.version);

// The game's Update button, in a look's own button style.
export function UpdateButton({ game, updates, className = "primary", tile = false }: { game: LibraryGame; updates: LibraryUpdates; className?: string; tile?: boolean }) {
  const plan = updates.plan(game);
  if (!plan) return null;
  return (
    <button className={className} disabled={updates.busy(plan)} onClick={() => void updates.update(plan)}>
      {tile ? (
        <>
          <Glyph name="update" size={22} stroke={1.6} />
          <span className="pn-action-label">{updates.progress(plan) || t("Update to {version}", { version: plan.version })}</span>
          <small>{planFacts(plan)}</small>
        </>
      ) : (
        updates.label(plan)
      )}
    </button>
  );
}
