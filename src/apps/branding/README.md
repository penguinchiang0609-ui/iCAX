# Application icons

`../Branding.Setting` configures startup window/taskbar icons. Paths are UTF-8,
relative to that setting file; absolute paths are also accepted.

```ini
platformIcon=branding/icax.ico
product.icax.tube-designer=tube-designer/webpage/assets/tube-designer.ico
```

The application counts **enabled product definitions**, not all installed folders:

- Exactly one product: use `product.<productId>`.
- Zero or multiple products: use `platformIcon`.
- A missing single-product icon falls back to the platform icon.
- A file that Windows cannot load falls back to the embedded iCAX icon.

Restart after changing the configuration or replacing an ICO. Switching products
or projects within the process does not switch the native taskbar icon.
The executable's static Explorer icon is iCAX; the running window uses the rule above.

`icax.svg` is the editable platform artwork: independent studio modules around a
shared workspace. TubeDesigner uses the hollow square-tube artwork maintained in
`../tube-designer/webpage/branding.mjs`.

Run `node src/apps/tube-designer/tools/build-branding.mjs` from the repository root
to regenerate both sets of PNG/ICO assets. The script uses Playwright; set
`ICAX_PLAYWRIGHT_MODULE` to its module URL and `ICAX_BROWSER_CHANNEL=msedge` if needed.
