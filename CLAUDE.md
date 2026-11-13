# Working rules

- This repository is public. Never commit secrets, personal income or tips, the restaurant name, or names of colleagues and guests. Read keys from the environment or from `../keys/`.
- Do not reference other companies' games, brands or artists in code, assets or docs. The design language is described in `tender-vault/10-concept/design-language.md`.
- Write code, identifiers, comments, commit messages, developer-facing program output and every `README.md` in English only. Russian text is allowed only as product content: on-screen labels, menu item names, speech phrases.
- Documentation lives in the `tender-vault` repository. Do not copy it here. When a component starts working, update its vault page in the same task.
- The default branch is `dev`. `main` is the release branch and is never ahead of `dev`. Agents work in `ai/<area>` branches. When the work is ready, the agent merges `ai/<area>` into `feature/<area>` (created from `dev` if it does not exist), pushes both and opens a pull request from `feature/<area>` into `dev`. The agent never merges into `dev` itself: Artemy Erkov reviews the pull request and merges it.
- `dev` goes to `main` only after Artemy Erkov carefully reviews `dev` and gives the command.
- Exception: changes to working rules only (`CLAUDE.md`, branch and process rules) go straight into `dev`, without a `feature/<area>` review.
- The vault records the state of `dev`: when a vault page describes this repository, it describes what is in `dev`, not what is only on `ai/*` or `feature/*`.
- Do not rewrite commit history.
