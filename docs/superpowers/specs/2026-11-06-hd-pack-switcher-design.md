# HD pack switcher

Approved by Kyle: “Yes, build the pack switcher as you described.”

Keep separate managed folders for Pikmin 1 HD 1080p and HD 4K under Load/Textures. Both the game and launcher offer Off plus installed packs with friendly names and measured sizes. Legacy GameID selections remain valid; marker labels identify old downloaded packs without moving user content. Changes apply on next start.

The launcher offers Delete per pack with exact path/size tooltips, confirmation, and a bounded two-line toast with freed bytes and location. Deleting an active pack sets Off. Validate containment and reject symlinks before deletion; never operate on game assets, saves or ISOs. Downloads install into their own variant without replacing another.

Use a shared catalog for identity, sizing, safe deletion and migration. Test it with disposable filesystem fixtures, exercise settings CLI, build Linux and inspect narrow/wide launcher renders. GPU runtime and macOS behavior remain separately untested without a disc and Mac run.
