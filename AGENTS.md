# Starlight visualisation release branch

This branch holds the installable CoreELEC ARM add-on that `kodi.addons` publishes.
Do not edit `addon.xml`, `visualization.starlight.so` or `resources/` by hand.
Change the source on `main`, then follow the release steps in the `main` branch `AGENTS.md`.
Run `python3 -m unittest discover -s tests` before every push.
