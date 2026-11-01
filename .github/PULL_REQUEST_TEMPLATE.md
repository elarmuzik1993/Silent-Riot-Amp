## Summary

<!-- What does this change, and why? -->

Closes #

## Testing

<!-- OS and compiler you built on, and what you ran. For GUI changes, attach a screenshot
     (made with `sr_editor_snapshot`, see `tools/README.md`). -->

## Checklist

- [ ] One change in this PR, with a test that covers it (or a note why none is possible)
- [ ] `bash scripts/verify.sh` passes (on Windows: `verify.sh --quick` plus the ctest targets)
- [ ] Tone lock: `tone_lock_tests` passes. If it fails only on your platform, say so here and **do not** regenerate the references (`docs/dev/tone-lock.md`)
- [ ] Tone-changing? If yes, sign-off run attached (`docs/dev/tone-signoff.md`): which gates moved, and by how much
- [ ] New plugin parameters, if any, are **appended** to `createParameterLayout()`
- [ ] New source files carry the SPDX licence header
- [ ] Docs updated where this change makes them stale (`docs/dev/`, `docs/manual/`, `CLAUDE.md`)
- [ ] No AI or bot attribution in commits or this PR (`CONTRIBUTING.md`); if you used AI tools, you have the right to license the result under GPL-3.0-or-later (`attribution/AI_USAGE.md`)
