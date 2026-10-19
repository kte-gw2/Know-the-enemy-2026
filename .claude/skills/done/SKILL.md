---
name: done
description: 'Close the build-test-fix loop on the current PR. Use when the user reports a test result: "tested, works", "tested ok", "done", "still broken", /done.'
---
# Done
The user tested the current PR in the real app and is reporting back.
No PR from this thread: ask for the PR number and stop.
**User says it works:**
1. Confirm CI is green on the latest commit. Red: stop and say what failed.
2. Merge the PR into the default branch and delete the branch.
3. If CLAUDE.md names a deploy step, ask once: deploy now? Run it only on yes.
4. Reply in three lines: merged commit, what shipped, and "resolve this thread".
**User says it is broken:**
1. Ask for exactly what they saw, one question, only if the report is vague.
2. Fix the root cause, push to the same branch, same PR.
3. Reply with what changed and what to test again. One test, not a list.
Never open a second PR for the same task. Never merge on a red CI.
