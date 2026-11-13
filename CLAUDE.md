# Working rules

- This repository is public. Never commit secrets, personal income or tips, the restaurant name, or names of colleagues and guests. Read keys from the environment or from `../keys/`.
- Do not reference other companies' games, brands or artists in code, assets or docs. The design language is described in `tender-vault/10-concept/design-language.md`.
- Write code, identifiers, comments, commit messages, developer-facing program output and every `README.md` in English only. Russian text is allowed only as product content: on-screen labels, menu item names, speech phrases.
- Documentation lives in the `tender-vault` repository. Do not copy it here. When a component starts working, update its vault page in the same task.
- The default branch is `dev`. `main` is the release branch and is never ahead of `dev`. Agents work in `ai/<area>` branches. When the work is ready, the agent merges `ai/<area>` into `feature/<area>` (created from `dev` if it does not exist) and pushes both. Artemy Erkov reviews the result on `feature/<area>` and accepts it. After acceptance, the agent merges `feature/<area>` into `dev` on Artemy's command.
- Do not rewrite commit history.
