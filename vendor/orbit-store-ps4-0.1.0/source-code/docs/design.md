# Visual specification

Direction (owner, 2026-10-03): the feel of the console's own storefront app, without its logos, names or licensed typeface. It is a TV storefront operated with a controller, not a dashboard.

Principles: the focused game's art fills the screen and the interface floats on it. Focus is the cursor: one white ring with a soft blue glow and a slight lift, everything else calm. Large titles use a light weight in sentence case. Depth comes from navy layers, not black.

Tokens: navy `#081329` base, `#0f1f3d` panels, `#1a2d52` inputs; text white, `#b9c4d8` secondary, `#8b99b3` tertiary; one action blue `#2a6df4`; success `#8be0b0`, error `#ff9a92`. System sans-serif, which on the console is the platform font. Sizes are in rem from a 20px root at 1920px wide (hero title 84px light, body 22px). Pill-shaped buttons and fields; 14px corners on art, 22px on dialogs. Motion: 160–180ms on focus and press, 600ms art crossfade; all of it is removed for the reduced-motion preference.

Screens:
- First run: two small toggle labels showing only each source's name, with round checks, neither selected initially. A download-rights and risk notice needs an explicit acknowledgement before saving. Either enabled source can populate Browse in 0.5.0. No library import or custom source controls.
- Discover: the focused tile updates the hero title, platform badge, genre, tagline and View game immediately. Background artwork waits for a 300ms pause in navigation, so holding the D-pad skips intermediate banner requests. Opening a game page shows that game's artwork immediately. The hero reserves two lines each for title and tagline to keep the rows steady; the complete title remains in the accessible text and game details. New on Orbit appears first, followed by Latest releases, then the All games grid, which keeps scrolling through the whole catalogue. Curated newOnOrbitRank picks lead New on Orbit; remaining recent additions are ordered by release date. Each platform edition has its own card, with sources and formats grouped inside it. Their virtual windows update when scrolling crosses a card boundary; layout measurements run on resize instead of every scroll frame. Visible cards receive focus directly; only an unmounted destination needs a render before focus. On the PS5 browser, row scrolling and Discover focus effects apply immediately instead of running a smooth-scroll or shadow animation on each move. Paired desktop and phone browsers retain their existing motion. Featured selection changes only through user navigation. Wide key art is shown full-bleed; square covers become a blurred colour wash because they are too small for a TV.
- Game page: full screen over the same art, not a dialog. Cover, title, badges, then Download options listing source, format, size, title ID and any known version. Select one option and choose storage. Direct options use Download to PS5; browser options use Open download page on PS5, with instructions to press Download on Vikingfile and return to Orbit. Size checks and existing queue status follow that exact option. Description and selected-option facts follow. Circle returns to the tile that opened it.
- Browse: search by game name or any option's title ID and one cover per game. Release date, newest first, is the default order; undated games follow alphabetically. Platform, source, format, region and size filters narrow matching options. Favourites and alternate title/size sorting remain available. Downloads: rows with cover, source, format, status, blue progress and actions.
- Top bar: Orbit mark, text tabs with a rounded highlight for the current tab, and round icon buttons for search, sources, storage and App settings that show their name when selected. Controller hints sit bottom right.
- Dialogs (sources, storage, pairing, auto-start, cancel): navy panels over a blurred backdrop; focus is trapped and restored. Sources edits the console-wide choices, explains the effect on unfinished downloads, and permits disabling both sources.

Focus rings follow controller and keyboard input. They are hidden after pointer or touch input, because console browsers do not reliably report focus-visible after programmatic focus. For a local PS5 browser session, a native click-only X follows navigation focus until actual pointer movement or touch takes ownership. Keyboard/gamepad echoes are suppressed so one press cannot also activate the newly opened page. Paired browsers retain normal mouse targeting. Phones get a wrapped tab row, a horizontal cover rail and a stacked game page, not a scaled-down TV layout.

Browser polling preserves unchanged JSON branches before publishing React state.
New queue progress, drive removal, source choices and catalogue revisions still
update immediately after a successful poll. An unchanged Library refresh cooldown
keeps its deadline instead of resetting it every two seconds. The backdrop and
Discover rails are memoized, so unrelated state changes do not rebuild them.
The ambient cover is filtered at one tenth of its displayed width and height,
then scaled up with the same visible blur; a working wide banner removes it.
This bounds fallback artwork work when both banner hosts are unavailable without
removing the cover wash or changing navigation. Desktop profiling is not proof
of PS5 frame rate; console acceptance remains separate.
