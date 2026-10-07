# Ultra Tab

Ultra Tab is a second app that runs beside lapis. Press its key from any app and
a translucent overlay deals the agents that need you as a deck of cards: one in
front, at most one peeking behind. Answer the front card and the next comes
forward. lapis keeps running as it is; Ultra Tab only reads what lapis publishes
and types to an agent the way the phone does.

This is milestone 1. It works from a build of this repository; it is not part of
the downloaded app yet.

## A card

Each card shows the agent's name, its folder and category, one sentence of what
happened (the start of the agent's last reply), and lapis's guess at your next
prompt ([suggestions](suggestions.md); with suggestions off, cards have no
guess). The top rail lists categories with how many agents wait in each; a quiet
column on the right names the agents at work, each with a pulsing dot.

Every card takes the same four answers:

| Key | Answer |
| --- | --- |
| Tab | Accept: send lapis's guess to that agent, as typed and submitted with Return |
| Hold Option | Speak: shows that it is listening. Voice input is not built yet; type instead |
| Typing, then Return | Type: start typing anywhere in the overlay; Return sends it to that agent |
| Left arrow | Skip: the card goes without sending anything and stays in this session's history |

While you are typing, the left arrow moves the cursor and Escape clears the text.
Escape with nothing typed puts the overlay away, as does clicking another app.
Command-[ and Command-] move through the categories on the rail.

A card whose agent waits on a request (a permission prompt, say) says so and
takes no typed answer: answer it in lapis, which shows the choices. Skipping it
still works.

A send that the agent's session refuses brings the card back with the reason.
An answered or skipped card stays away until the agent has something new: its
next finished turn, a new guess or a new request.

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
open build/desktop/apps/ultratab/lapis_ultratab.app
```

The lapis window must come from a build that publishes `agent_state.json`
(below); restart it once after updating.

Ultra Tab has no Dock icon. The left Option key with Space shows or hides it;
the right Option key with Space is left alone. It never comes
forward or takes the keyboard on its own: a new card arriving while you work in
another app changes nothing until you press the key.

Options:

- `--hotkey Command-Shift-U` uses another key (modifiers Command, Option,
  LeftOption, RightOption, Control, Shift, then Space, a letter, a digit, F1 to F12, Return, Tab or
  Escape; one of Command, Option or Control is required). A `hotkey` in
  `~/.lapis/ultratab.json` does the same: `{"hotkey": "Control-Option-Space"}`.
- `--home FOLDER` reads another lapis data folder (default: `LAPIS_HOME`, else
  `~/.lapis`).
- `--show` shows the overlay at launch.
- `--list` prints the deck in order and exits, without a window.

## What it reads and how it answers

Ultra Tab never takes lapis's workspace lock and never writes lapis's files; its
own files there are named `ultratab_*` (the composed cards, their log and the
composer's helper in `ultratab_compose/`). It reads two files in lapis's
`runtime/` folder whenever they change:

- `workspace.json`, the registry: agents, categories, folders and each agent's
  session endpoint and launch.
- `agent_state.json`, which the lapis window publishes for it: each agent's
  status, unseen mark, pending requests, when it began to wait, and the guess
  shown with the reply it answers. A lapis built before this file existed shows
  no cards; Ultra Tab says so at the bottom of the overlay. When lapis has
  closed, Ultra Tab shows what it last published and says that too.

To answer, it joins the agent's session service as an extra view (as the phone
does), so the lapis window keeps its own connection and its screen. It never
resizes the terminal, sends the text and Return as one paste, and leaves. The
session refuses that paste while a request is pending. An agent whose service is
too old to be joined is reported, never taken over.

lapis records what you sent to an agent in its suggestion log when the next turn
ends, as it does for any prompt; a guess sent from Ultra Tab is recorded as typed,
not as taken with Tab.

## Not yet

- Voice: holding Option shows the listening state only.
- History is kept only while Ultra Tab runs; nothing is searchable later.
- The overlay does not show composed cards yet; they are written for it.
- The learned Tab order and other ranking beyond lapis's tiers.
- Answering requests from the deck.
- A packaged app, a login item and a settings view.
- The blur behind the overlay (macOS `NSVisualEffectView`) and the global key
  have not been exercised by an automated check; the overlay's layout and the
  four answers are checked offscreen.
