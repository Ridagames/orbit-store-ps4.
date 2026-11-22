# Announcements

Orbit shows short notices that you post yourself, for example new games in the catalogue or a new look. Put them in `announcements.json` at the root of the public repo, next to `payloads.json`:

`https://raw.githubusercontent.com/saawant12/orbit-store-ps4/main/announcements.json`

Orbit 1.8.4 and later read it. Older versions ignore it.

## Where they show

- **Browser version:** a notice at the top, like the update notice, with **Dismiss**.
- **Orbit Zero:** a note at the top, like its other notes, with a close button.
- **Native TV app:** the title, as a short note at the top for 8 seconds, like "An update is available".

Each app shows one announcement at a time, in file order, and shows each one once: after it is closed (or, on the TV app, after its note has shown) that app doesn't show it again. Each app keeps its own list, so closing one on the PS4 doesn't close it in the other app.

## The file

```json
{
  "announcements": [
    {
      "id": "new-games-2026-11-03",
      "title": "10 new games",
      "message": "Find them in New on Orbit on Discover.",
      "translations": {
        "de": { "title": "10 neue Spiele", "message": "Du findest sie unter Neu bei Orbit in Entdecken." },
        "zh-Hans": { "title": "10 款新游戏" }
      },
      "startsAt": "2026-11-03",
      "endsAt": "2026-11-10T00:00:00Z",
      "minVersion": "1.8.4",
      "maxVersion": "1.9.0"
    }
  ]
}
```

| Field | Needed | What it does |
|---|---|---|
| `id` | Yes | Names the announcement. Lowercase letters, digits and hyphens, up to 64. The apps remember it once shown, so give every new announcement a new `id`. Reusing an `id` keeps it hidden where it was already seen. |
| `title` | Yes | Up to 120 bytes, one line. It's the only text the TV app shows, so make it stand on its own. |
| `message` | No | Up to 600 bytes, one paragraph, no line breaks. |
| `translations` | No | By language code (`de`, `fr`, `pt-BR`, `zh-Hans`, `zh-Hant`, `ja`, `ko` and the rest the apps offer). Each has a `title` and, if wanted, a `message`. An app in another language shows the English text. `pt` also covers `pt-BR` and `pt-PT`. |
| `startsAt` | No | First day or moment it shows, in UTC: `2026-11-03` or `2026-11-03T18:00:00Z`. |
| `endsAt` | No | When it stops showing, in the same form. |
| `minVersion` / `maxVersion` | No | Only Orbit versions from `minVersion` up to and including `maxVersion` show it, for example `"maxVersion": "1.8.4"` to tell older versions about something new in 1.9.0. |

At most 10 current, valid announcements are returned, in file order; the file can be up to 64 KB.

## When it doesn't work

If the file is missing, nothing shows. An unreadable or invalid file leaves the last valid copy in use, if there is one. An announcement that breaks a rule above (a bad `id`, an over-long title, a date it can't read) is left out on its own, and the others still show. A translation that breaks a rule falls back to the English text.

The service reads the file at most once an hour and keep the last valid copy, so announcements still show while GitHub can't be reached. Fetching runs in the background so it doesn't block transfers or controls. Deleting the file removes them at the next successful read. The apps ask every 10 minutes, so a new announcement appears within about an hour. Browser and TV clients check again shortly while a PS4 fetch is in progress.

## Posting one

1. Edit `announcements.json` in the public repo and add the entry at the top.
2. Check it's valid JSON before committing.
3. Remove old entries once they have ended to keep the file small.

To clear everything, leave an empty list, `{ "announcements": [] }`, or delete the file.
