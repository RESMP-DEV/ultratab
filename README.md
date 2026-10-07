# Ultra Tab

Ultra Tab is a small standalone app that runs beside lapis. It sits in the
background with no Dock icon; press its key from any app and a translucent
overlay deals the agents that need you as a deck of cards: one in front, at most
one peeking behind. Answer the front card and the next comes forward. lapis
keeps running as it is; Ultra Tab only reads what lapis publishes and types to
an agent the way the phone does.

It works from a build of this repository; it is not part of the downloaded app.

## A card

Each card shows the agent's name, its folder and category, a headline and the
proposed reply. The top rail lists categories with how many agents wait in each;
a quiet column on the right names the agents at work, each with a pulsing dot.

A plain card's headline is one sentence of what happened (the start of the
agent's last reply) and its proposed reply is lapis's guess at your next prompt
([suggestions](suggestions.md); with suggestions off, plain cards have no
guess). A composed card (below) adds a dim "since" line above the headline, its
own headline and up to three blocks: a paragraph, a short list, a table, a
diagram or a link, and its own proposed reply.

Every card takes the same four answers, shown as keys along the bottom that
light up when pressed:

| Key | Answer |
| --- | --- |
| Tab | Accept: send the proposed reply to that agent, as typed and submitted with Return |
| Hold Option | Speak: shows that it is listening. Voice input is not built yet; type instead |
| Typing, then Return | Type: start typing anywhere in the overlay; Return sends it to that agent |
| Left arrow or Delete | Skip: the card goes without sending anything and stays in this session's history |

While you are typing, the left arrow moves the cursor, Delete erases and Escape
clears the text.
Escape with nothing typed puts the overlay away, as does clicking another app.
Command-[ and Command-] move through the categories on the rail. Command-O opens
the card's first link and Command-click opens any link, with the system's
default handler; the card stays. A plain click on a link opens nothing.

A card whose agent waits on a request (a permission prompt, say) says so and
takes no typed answer: answer it in lapis, which shows the choices. Skipping it
still works.

A send that the agent's session refuses brings the card back with the reason.
An answered or skipped card stays away until the agent has something new: its
next finished turn, a new guess or a new request.

The front card slides away (right when answered, left when skipped) while the
next comes forward, in about 180 ms. Keys act at once: a second Tab during the
slide already answers the next card. With Reduce Motion on in macOS
accessibility settings, cards change without sliding and the dots do not pulse.

## Order

Cards come in the order lapis's Tab visits waiting agents: a guess you have not
seen yet, then turns that finished unseen and requests, then guesses you have
seen; within each, the agent that has waited longest first. Agents at work, and
agents whose state is unknown, are never cards; a pending request always is.

## Composed cards

For each agent that waits on you, Ultra Tab also composes a card meant to put
you back in that thread in seconds: when you last looked at it and how many
turns happened since, a one-line summary, and at most three blocks the model
chooses (a short paragraph, a list, a table when there are numbers to compare,
a small diagram when structure is the point, or a link to a report the agent
wrote that you have not opened). Its proposed next message is lapis's guess
when there is one.

A card is composed when the agent has a newer finished turn or a new guess than
the card was composed for, after two seconds of quiet, at most two at a time,
the card in front first. A guess that arrives for a turn already composed only
replaces the proposed message, with no second model call. While the overlay
shows, the card in front changes only when what it was composed for changes.

The composer reads the end of the conversation from the transcript Claude Code
or Codex keeps (as [suggestions](suggestions.md) do, with the same clipping),
when you last focused, typed or pasted into that agent in lapis (from
`runtime/interaction.jsonl`, when the interaction log is on), how long the
agent has waited, its title and category, and the HTML and Markdown files it
wrote or mentioned since you last looked. It asks the model lapis's suggestions
use, through the Claude Code CLI on the plan it is signed in to; no API key is
read. A link is kept only when it names one of those files or a URL the agent
wrote; a diagram is kept only after scripts, external references and event
attributes are removed. When composing fails, the card is the agent's last
message on one line. For an agent on another machine the transcript is not
read; its card is composed from the reply lapis's guess answers, when there is
one.

Settings go in `~/.lapis/ultratab.json`:

```json
{"composer": {"enabled": true, "model": "claude-opus-5-5", "effort": "",
              "timeoutSeconds": 120}}
```

`"endpoint": "http://127.0.0.1:8000/v1"` with a `"model"` sends the request to
a local OpenAI-compatible server (`/chat/completions`) instead, for example a
local Qwen model. It is off unless set. `"enabled": false` stops composing.

Cards are written to `runtime/ultratab_cards.json` (owner-only, replaced
atomically, only when a card changes, at most 512 KiB):

```json
{"v": 1, "cards": {"<agent id>": {
  "key": "<the guess key, or turn:<ms> for the finished turn>",
  "composed": "2026-10-07T06:01:45Z", "model": "claude-opus-5-5",
  "since": "You last looked 3 h ago; 2 turns since",
  "tldr": "<one line>",
  "blocks": [{"type": "text", "text": "..."}, {"type": "list", "items": ["..."]},
             {"type": "table", "columns": ["..."], "rows": [["..."]]},
             {"type": "diagram", "svg": "<svg viewBox=...>...</svg>"},
             {"type": "link", "label": "...", "url": "file:///..."}],
  "prompt": "<the proposed next message>"}}}
```

Limits: three blocks; text 300 characters; lists 6 items of 120; tables 5
columns, 8 rows, cells of 60; diagrams 16 KB; link labels 60. Each composition
is logged to `runtime/ultratab_compose.jsonl` with its time, agent, key,
duration, model and outcome, never conversation text.

## Running it

With the desktop build configured ([build](build.md)):

```sh
cmake --build build/desktop --target lapis_ultratab
open "build/desktop/apps/ultratab/Ultra Tab.app"
```

A signed copy, built like the downloadable lapis (its Qt from pinned source plus
Qt SVG, the Developer ID identity, the hardened runtime), with the icon rendered
from `apps/ultratab/icon/icon.svg`:

```sh
uv run --no-project python scripts/package_macos.py qt   # once
uv run --no-project python scripts/package_ultratab.py app
```

It is left in `build/release/ultratab/stage/Ultra Tab.app`; move it to
Applications yourself. It is not notarized yet.

The lapis window must come from a build that publishes `agent_state.json`
(below); restart it once after updating.

The left Option key with Space shows or hides the overlay; the right Option key
with Space does nothing. macOS registers the chord for both Option keys, so other
apps do not receive right Option-Space while Ultra Tab runs. It never comes forward or takes the keyboard on its
own: a new card arriving while you work in another app changes nothing until you
press the key. Drag the overlay by its background to move it; it keeps that place
on that screen across launches (in `ultratab-window.json` beside `ultratab.json`)
and keeps its size.

Settings, in `~/.lapis/ultratab.json` (or the `--home` folder):

```json
{"hotkey": "Control-Option-Space", "startAtLogin": true}
```

- `hotkey` uses another key (modifiers Command, Option, LeftOption, RightOption,
  Control, Shift, then Space, a letter, a digit, F1 to F12, Return, Tab or
  Escape; one of Command, Option or Control is required). `--hotkey` does the
  same for one launch.
- `startAtLogin` (default on) registers Ultra Tab as a login item. Only a copy in
  an Applications folder registers itself; a build-folder copy leaves login
  items alone. Set it to `false` and launch once to remove it.

Options:

- `--home FOLDER` reads another lapis data folder (default: `LAPIS_HOME`, else
  `~/.lapis`).
- `--show` shows the overlay at launch.
- `--list` prints the deck in order and exits, without a window.

## What it reads and how it answers

Ultra Tab never takes lapis's workspace lock and never writes lapis's files; its
own files there are named `ultratab_*` (the composed cards, their log and the
composer's helper in `ultratab_compose/`). It reads three files in lapis's
`runtime/` folder whenever they change:

- `workspace.json`, the registry: agents, categories, folders and each agent's
  session endpoint and launch.
- `agent_state.json`, which the lapis window publishes for it: each agent's
  status, unseen mark, pending requests, when it began to wait, and the guess
  shown with the reply it answers. A lapis built before this file existed shows
  no cards; Ultra Tab says so at the bottom of the overlay. When lapis has
  closed, Ultra Tab shows what it last published and says that too.
- `ultratab_cards.json`, composed cards written by a separate composer (below).
  Without it, or for an agent it does not cover, cards are plain.

To answer, it joins the agent's session service as an extra view (as the phone
does), so the lapis window keeps its own connection and its screen. It never
resizes the terminal, sends the text and Return as one paste, and leaves. The
session refuses that paste while a request is pending. An agent whose service is
too old to be joined is reported, never taken over.

lapis records what you sent to an agent in its suggestion log when the next turn
ends, as it does for any prompt; a reply sent from Ultra Tab is recorded as
typed, not as taken with Tab.

## Composed cards

`runtime/ultratab_cards.json` (at most 8 MB, replaced by renaming):

```json
{"v": 1, "cards": {"<session id>": {
  "key": "<turn key>", "composed": "2026-10-06T21:04:00Z", "model": "...",
  "since": "You last looked 3 h ago; 2 turns since",
  "tldr": "one line",
  "blocks": [
    {"type": "text", "text": "..."},
    {"type": "list", "items": ["..."]},
    {"type": "table", "columns": ["..."], "rows": [["..."]]},
    {"type": "diagram", "svg": "<svg ...>"},
    {"type": "link", "label": "...", "url": "file:///... or https://..."}
  ],
  "prompt": "the proposed reply"}}}
```

The session id is the agent's registry id. A composed card is shown only while
its `key` names the agent's current turn: the composer's key (lapis's guess
key when there is a guess, else `turn:<turnAtMs>`, with the time the agent began
waiting standing in for a turn that finished before the lapis window started),
or the `agent_state.json` values `<turnAtMs>|<neededAtMs>|<offer key>|<requests>`
joined by `|`, optionally prefixed with `<session id>|`. Any other key is stale
and the plain card shows. A card without `tldr` keeps the plain
headline; one without `prompt` keeps lapis's guess. A request card never takes a
composed prompt.

What is shown, and the limits applied on reading:

- At most three blocks, in order; unknown or invalid blocks are skipped.
- `text` shows up to five lines; `list` up to six items of two lines each.
- `table`: up to six columns and eight rows (more rows are counted, not shown).
  Columns whose filled cells are all numbers ("1,024", "-3.5%", "12 ms",
  "2.1x") are right-aligned in tabular figures; text columns give way and elide
  when the table is wider than the card.
- `diagram`: SVG up to 256 KB, drawn to fit the card at most 200 points tall.
  It is rejected (and skipped) when it has `script`, `foreignObject`, other
  embedded content, event-handler attributes, a DTD or entity, a processing
  instruction, or any reference outside the document (`href` or `url()` that
  is not `#id`, CSS `@import`). An SVG root without a `fill` is given a light
  one, so write diagrams for a dark background.
- `link`: only `https:` with a host and local `file:///` paths; anything else
  is skipped.

## On the iPhone

Ultra Tab for iPhone (`apps/ios/UltraTab`) is a separate app with the same
icon. It deals the same deck, in the same order, one card at a time: the
agent's name, folder and category, the "since" line, the headline, up to three
blocks (paragraph, list, table, diagram, link) and the proposed reply. It
reaches the Mac through the [phone gateway](phone.md), so it needs the same
Tailscale or ZeroTier setup as the lapis phone app and nothing else.

Every card takes three answers:

| Gesture | Answer |
| --- | --- |
| Swipe right (or Send) | Accept: send the proposed reply |
| Swipe left (or Skip) | Skip: the next card comes forward; nothing is sent |
| The voice button | Annotate: tap to start and again to stop, or hold while speaking; the words land in the field above it |

The annotation field is ordinary text: edit what was heard or type with the
keyboard, then press the arrow to send it. Nothing is sent until then.
Dictation uses Apple's speech recognizer, on the iPhone when it supports that;
the first use asks for speech recognition and microphone access. A tap on a
web link opens it in the browser; a link to a file names it as being on the
Mac. A card waiting on a request springs back from a right swipe: answer it in
lapis. The chips on top choose a category, as Command-[ and Command-] do on the
Mac.

An answer joins the agent's session beside the lapis window, sends one paste
and Return, and leaves, as the Mac overlay does; the window keeps its
connection and the terminal keeps its size. A send the session refuses brings
the card back with the reason. Answers and skips are kept on the phone until
the app quits; the Mac overlay keeps its own.

Install it on the phone (unlocked, on the same Wi-Fi as the Mac or on a cable)
with:

```sh
uv run --no-project python scripts/install_ios_app.py --app ultratab
```

`uv run --no-project python scripts/check_ultratab_ios.py` runs its unit and UI
tests in a headless simulator against real session services and the gateway
(`--unit` for the unit tests alone); screenshots go to
`build/ios-ultratab/screens/`.

## Not yet

- Voice on the Mac: holding Option shows the listening state only. The iPhone
  app dictates with Apple's recognizer behind a small `Transcriber` protocol,
  so another speech model can replace it.
- History is kept only while Ultra Tab runs; nothing is searchable later.
- The overlay does not show composed cards yet; they are written for it.
- The learned Tab order and other ranking beyond lapis's tiers.
- Answering requests from the deck.
- Notarization, a settings view and an update feed for the packaged app.
- The blur behind the overlay (macOS `NSVisualEffectView`), the global key,
  dragging and the login item have not been exercised by an automated check;
  the overlay's layout, composed cards and the four answers are checked
  offscreen.
