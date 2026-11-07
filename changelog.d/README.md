# Changelog fragments

A pull request that changes behaviour a user can see adds one file here, named
`<slug>.<type>.md`, where `<type>` is `added`, `changed`, `deprecated`, `removed`, `fixed` or
`security`. The content is one paragraph that starts with a bold summary:

```markdown
**Greeting accepts an empty name.** `greet("")` now returns the generic greeting.
```

Do not edit `CHANGELOG.md`. At release time `changelog_tool.py assemble` (in `icl-common-infra`)
moves the fragments into a new section and deletes them. A pull request that changes only
documentation, tests or tooling needs no fragment.
