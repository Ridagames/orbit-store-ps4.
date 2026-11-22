// Crossbar's Library: views as folders, the games as a list with the focused
// game's buttons on its row and its details beside it, then Files (the drives
// and their folders: CrossbarFiles.tsx), the scan and game sizes. Actions,
// confirmations and the scan are Library's own.
import { useState, type ReactNode } from "react";
import { bytes, type LibraryGame, type Storage } from "../types";
import type { LibraryModel } from "../useLibrary";
import { placeName, t, tn } from "../i18n";
import { Artwork } from "../Artwork";
import { GameActions } from "../LibraryActions";
import { CrossbarList, type XbRow } from "./CrossbarList";
import { CrossbarFiles } from "./CrossbarFiles";
import type { FilesModel } from "../useFiles";
import type { LibrarySection } from "../Library";
import { Glyph, type GlyphName } from "./Glyph";
import { libraryStatus as statusOf, planFacts, UpdateButton, versionStep, type LibraryUpdates } from "../LibraryUpdates";

const viewGlyph: Record<string, GlyphName> = { Installed: "check", "On drive": "drive", Unavailable: "warn", Updates: "update" };
const inView = (g: LibraryGame, v: string) =>
  v === "Updates" ? !!g.update : v === "Installed" ? g.installed : v === "On drive" ? g.onDrive : !g.onDrive;

export function CrossbarLibrary({
  model,
  games,
  rows,
  views,
  view,
  setView,
  section,
  setSection,
  ready,
  busy,
  error,
  openScan,
  dialogs,
  drives,
  files,
  consoleBrowser,
  updates,
}: {
  updates: LibraryUpdates;
  model: LibraryModel;
  games: LibraryGame[];
  rows: LibraryGame[];
  views: readonly string[];
  view: string;
  setView: (view: string) => void;
  section: LibrarySection;
  setSection: (section: LibrarySection) => void;
  ready: boolean;
  busy: boolean;
  error: string;
  openScan: () => void;
  dialogs: ReactNode;
  drives: Storage[];
  files: FilesModel;
  consoleBrowser: boolean;
}) {
  const [current, setCurrent] = useState<string | undefined>();
  const folderRow = (key: string, label: string, sub: string, glyph: GlyphName, open: () => void): XbRow => ({
    key,
    activate: open,
    render: () => (
      <>
        <span className="xb-row-icon">
          <Glyph name={glyph} size={22} />
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">{label}</span>
          <span className="xb-row-sub">{sub}</span>
        </span>
        <Glyph name="next" size={18} />
      </>
    ),
  });
  const leave = () => {
    if (view !== views[0]) {
      setCurrent(`folder-${view}`);
      setView(views[0]);
      return true;
    }
    return false;
  };
  // Files works without ShadowMount, so it comes before Library's own state.
  if (section === "files")
    return (
      <CrossbarFiles
        files={files}
        drives={drives}
        consoleBrowser={consoleBrowser}
        leave={() => {
          setSection("games");
          setCurrent("folder-files");
        }}
      />
    );
  if (!ready) {
    return (
      <section className="xb-page xb-library">
        <div className="xb-split">
          <div className="xb-column">
            <div className="xb-empty">
              <h2>{busy ? t("Finding your games") : t("Connect your library")}</h2>
              <p>
                {busy
                  ? t("Reading installed titles and drive availability.")
                  : t("Library isn’t responding on this PS4. Select Try again.")}
              </p>
              {error && <p className="warn">{error}</p>}
              {!busy && (
                <button className="xb-pill primary" disabled={Date.now() < model.refreshAt} onClick={() => void model.read(true)}>
                  {t("Try again")}
                </button>
              )}
            </div>
          </div>
        </div>
        {dialogs}
      </section>
    );
  }
  const focusedGame = rows.find((g) => `game-${g.titleId}` === current) || rows[0];
  const focusedPlan = focusedGame ? updates.plan(focusedGame) : null;
  const gameRows: XbRow[] = rows.map((g) => ({
    key: `game-${g.titleId}`,
    disabled: !g.onDrive,
    render: () => (
      <>
        <span className="xb-row-art">
          {g.cover ? (
            <Artwork src={g.cover} fallbackSrc={g.coverFallback || undefined} alt="" decoding="async" referrerPolicy="no-referrer" />
          ) : (
            <Glyph name="box" size={22} />
          )}
        </span>
        <span className="xb-row-text">
          <span className="xb-row-title">
            <bdi>{g.title}</bdi>
            {g.update && <span className="xb-tag xb-update-tag">{t("Update {version}", { version: g.update.version })}</span>}
          </span>
          <span className={`xb-row-sub${!g.onDrive ? " warn" : ""}`}>
            {statusOf(g)} · {g.onDrive ? placeName(g.location) : t("Drive disconnected")}
          </span>
        </span>
      </>
    ),
    actions: <GameActions game={g} model={model} driveCards lead={<UpdateButton game={g} updates={updates} />} />,
  }));
  // Inside Updates: Update all first.
  const updateAll: XbRow[] =
    view === "Updates"
      ? [
          {
            key: "update-all",
            compact: true,
            disabled: updates.allDisabled,
            activate: () => !updates.allDisabled && void updates.updateAll(),
            render: () => (
              <>
                <span className="xb-row-icon">
                  <Glyph name="update" size={22} />
                </span>
                <span className="xb-row-text">
                  <span className="xb-row-title">{t("Update all")}</span>
                </span>
                <span className="xb-row-value">{updates.allSummary}</span>
              </>
            ),
          },
        ]
      : [];
  const allRows: XbRow[] = [
    ...updateAll,
    ...(view === views[0]
      ? views.slice(1).map((v) =>
          folderRow(`folder-${v}`, t(v), tn(games.filter((g) => inView(g, v)).length, "{count} game", "{count} games"), viewGlyph[v] || "folder", () => {
            setView(v);
            setCurrent(undefined);
          }),
        )
      : []),
    ...gameRows,
    ...(view === views[0]
      ? [
          folderRow("folder-files", t("Files"), tn(drives.length, "{count} drive", "{count} drives"), "folder", () => {
            setSection("files");
            setCurrent(undefined);
          }),
          ...(model.can("rescan")
            ? [
                folderRow(
                  "scan",
                  t("Scan for games"),
                  model.snapshot?.scanning ? t("Looking for new games…") : t("Check installed packages"),
                  "scan",
                  openScan,
                ),
              ]
            : []),
        ]
      : []),
  ];
  return (
    <section className="xb-page xb-library">
      <div className="xb-split">
        <div className="xb-column">
          {view !== views[0] && (
            <p className="xb-breadcrumb">
              {t("Library")} <span aria-hidden="true">›</span> {t(view)}
            </p>
          )}
          <CrossbarList rows={allRows} current={current || (focusedGame ? `game-${focusedGame.titleId}` : undefined)} setCurrent={setCurrent} back={leave} ariaLabel={t("Library")} />
          {updates.error("all") && <p className="xb-note warn" role="alert">{updates.error("all")}</p>}
        </div>
        {focusedGame && (
          <aside className="xb-aside xb-card" aria-live="polite">
            {focusedPlan && (
              <div className="xb-update-card">
                <p className="xb-card-kicker">{t("Update {version}", { version: focusedPlan.version })}</p>
                <p className="xb-card-line">
                  {focusedPlan.from ? t("Installed {version}", { version: versionStep(focusedPlan) }) : t("Update to {version}", { version: focusedPlan.version })}
                </p>
                <p className="xb-card-fine">{planFacts(focusedPlan)}</p>
                {updates.error(focusedGame.titleId) && <p className="xb-card-error">{updates.error(focusedGame.titleId)}</p>}
              </div>
            )}
            <p className="xb-card-kicker">
              {focusedGame.platform === "unknown" ? t("Platform unknown") : focusedGame.platform.toUpperCase()} · {focusedGame.titleId}
              {model.stale && <span className="xb-tag">{t("Saved snapshot")}</span>}
            </p>
            <h2><bdi>{focusedGame.title}</bdi></h2>
            <dl className="xb-facts">
              <div>
                <dt>{t("Installation")}</dt>
                <dd>{focusedGame.installed ? t("Registered on the console") : t("Not installed on the console")}</dd>
              </div>
              <div>
                <dt>{t("Mount status")}</dt>
                <dd>{focusedGame.mounted ? t("Mounted") : t("Not mounted")}</dd>
              </div>
              <div>
                <dt>{t("Source")}</dt>
                <dd>{focusedGame.onDrive ? t("Available on drive") : t("Unavailable; the drive or source may be missing")}</dd>
              </div>
              <div>
                <dt>{t("Format")}</dt>
                <dd>{focusedGame.format}</dd>
              </div>
              <div>
                <dt>{t("Source path")}</dt>
                <dd className="library-path">{focusedGame.path || t("Not reported")}</dd>
              </div>
              {focusedGame.runtimePath && focusedGame.runtimePath !== focusedGame.path && (
                <div>
                  <dt>{t("Game path")}</dt>
                  <dd className="library-path">{focusedGame.runtimePath}</dd>
                </div>
              )}
              <div>
                <dt>{t("Source size")}</dt>
                <dd>
                  {focusedGame.sizeBytes !== null
                    ? bytes(focusedGame.sizeBytes)
                    : focusedGame.sizeStatus === "unavailable"
                      ? t("Unavailable")
                      : model.snapshot?.measuring
                        ? t("Measuring…")
                        : t("Not measured")}
                </dd>
              </div>
              <div>
                <dt>{t("Size reported by console")}</dt>
                <dd>{focusedGame.installedSizeBytes === null ? t("Not reported") : bytes(focusedGame.installedSizeBytes)}</dd>
              </div>
            </dl>
            {focusedGame.foundByOrbit && (
              <p className="xb-card-fine">
                {t(
                  "Orbit found this game itself; the running games service didn’t list it. For the most complete list, keep that service up to date.",
                )}
              </p>
            )}
            <p className="xb-card-fine">
              {t(
                "Installed means registered on the console. Mounted means its source is currently mounted. On drive means the source is available. These statuses do not confirm that a game will launch.",
              )}
            </p>
            {error && <p className="xb-card-error">{error}</p>}
          </aside>
        )}
      </div>
      {dialogs}
    </section>
  );
}
