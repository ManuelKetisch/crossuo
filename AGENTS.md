# CrossUO fork

CrossUO owns client rendering, input, gumps, local settings, and client-side
packet handling. ModernUO owns authoritative game rules, inventory, and the
saved world. Preserve wire compatibility with existing server and client paths.

- Read [CrossUO's build and operation notes](README.md) and relevant files in `docs/`.
- For the paired Britannia Solo workspace, start at
  [shared development](../ModernUO/dev-docs/uoex-development/README.md),
  [memory](../ModernUO/.memory/index.md), and
  [skills](../ModernUO/.agents/skills/README.md) when ModernUO is present.
- Read the [client development skill](../ModernUO/.agents/skills/crossuo-client/SKILL.md)
  for renderer, interaction, and protocol changes in the paired workspace.

Work on the owner's requested branch or in a separate worktree. Preserve
unrelated edits, including work in progress on the menubar. Keep local client
profiles, captures, credentials, and copyrighted game data out of commits.
Use the normal client entry point when checking input or gump behavior.
Commit and push only when the owner asks, to the owner's fork by default.
