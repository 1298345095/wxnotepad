# Domain Docs

How engineering skills should consume this repo's domain documentation.

## Before exploring, read these

- `GLOSSARY.md` at the repo root, if it exists.
- `docs/adr/`, reading ADRs that touch the area being explored.

If these files do not exist, proceed silently. Create them lazily when domain terms or decisions are actually resolved.

## File structure

This is a single-context repository:

/
├── GLOSSARY.md
├── docs/adr/
└── src/

## Use the glossary's vocabulary

When output names a domain concept, use the term defined in `GLOSSARY.md`. If the needed concept is absent, note the gap for domain modeling.

## Flag ADR conflicts

If output contradicts an existing ADR, surface the conflict explicitly rather than silently overriding it.
