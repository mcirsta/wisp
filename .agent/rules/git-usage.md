---
trigger: always_on
---

# Git Usage Restriction
This is a **hard limit** for all AI agents working in this workspace.

## Rules:
1. **Read-Only Only**: You are only allowed to use read-only git operations by default.
   - Allowed commands include: `git log`, `git status`, `git diff`, `git show`, `git branch`, etc.
2. **No Mutations**: Do **NOT** perform any mutating git operations unless the USER specifically instructs you to do so for a specific task.
   - Prohibited commands include: `git commit`, `git push`, `git merge`, `git rebase`, `git reset`, `git checkout` (to change branches/state), `git add`, etc.
