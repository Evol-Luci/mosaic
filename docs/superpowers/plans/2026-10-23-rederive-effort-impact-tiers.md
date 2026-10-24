# Re-derive the Effort vs. impact tiers — plan

Spec: `docs/superpowers/specs/2026-10-23-rederive-effort-impact-tiers-design.md`.

1. Commit spec + plan (predictions on record before any content change).
2. Write the checker (scratch, not committed): live-bullet count and title multiset, anchor
   resolution, tier-row traceability. Run it on a deliberately broken copy; keep the red output.
3. Commit A: fold the `LiveLinkServer` duplicate into the record and delete it; strike the
   `ctest -j` bullet and tier row; run `archive_shipped_backlog.py` (dry-run first).
4. Commit B: same for the other 8 duplicates, ENet, impulse.
5. Commit C: move the section's history to the record by hand (the tool handles bullets and
   table rows, not prose blocks); write the new tiers; add the spec's Non-goals to the backlog.
6. Update `next-steps.md` (twelve -> real count; item 2 done; promote next candidate).
7. Run checker, tool tests, pre-commit; PR.
