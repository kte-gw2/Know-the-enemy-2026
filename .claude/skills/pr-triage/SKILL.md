---
name: pr-triage
description: Turn the open pull request list into a short task board. Use for "what is open", "where was I", "triage PRs", /pr-triage.
---
# PR triage
The open PR list is the task list. Threads are not.
1. List every open PR: number, title, age in days, CI status, last activity.
2. Put each in one group:
   - **Needs test**: CI green, nobody tested it in game yet.
   - **Broken**: CI red or merge conflict.
   - **Stale**: no commit and no comment for 14 days. Suggest closing it; the user decides.
3. Print the board, most urgent group first. One line per PR.
4. End with one line: how many are open, and the one PR to deal with first.
Change nothing. Never merge or close here; the user decides, then uses `done`.
