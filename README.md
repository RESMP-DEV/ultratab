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

## Running it

With the desktop build configured ([build](build.md)):

```sh
cmake --build build/desktop --target lapis_ultratab
open build/desktop/apps/ultratab/lapis_ultratab.app
```

The lapis window must come from a build that publishes `agent_state.json`
(below); restart it once after updating.

Ultra Tab has no Dock icon. Option-Space shows or hides it. It never comes
forward or takes the keyboard on its own: a new card arriving while you work in
another app changes nothing until you press the key.

Options:

- `--hotkey Command-Shift-U` uses another key (modifiers Command, Option,
  Control, Shift, then Space, a letter, a digit, F1 to F12, Return, Tab or
  Escape; one of Command, Option or Control is required). A `hotkey` in
  `~/.lapis/ultratab.json` does the same: `{"hotkey": "Control-Option-Space"}`.
- `--home FOLDER` reads another lapis data folder (default: `LAPIS_HOME`, else
  `~/.lapis`).
- `--show` shows the overlay at launch.
- `--list` prints the deck in order and exits, without a window.

## What it reads and how it answers

Ultra Tab never takes lapis's workspace lock and never writes lapis's files. It
reads two files in lapis's `runtime/` folder whenever they change:

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
- The learned Tab order and other ranking beyond lapis's tiers.
- Answering requests from the deck.
- A packaged app, a login item and a settings view.
- The blur behind the overlay (macOS `NSVisualEffectView`) and the global key
  have not been exercised by an automated check; the overlay's layout and the
  four answers are checked offscreen.
