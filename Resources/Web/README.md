# Web pages shown inside the app

- `fretboard.html`: the **Fretboard** tab (Source/FretboardPanel.cpp). Embedded in the app at build time and served
  from inside it, so it works offline.
- `fonts/Archivo.ttf`: the page's font (SIL Open Font License, `fonts/Archivo-OFL.txt`).

## Updating the Fretboard page

Replace `fretboard.html` with the new version (same file name) and rebuild. The file is used as it is; when served,
the app:

- drops `<link>` tags that load Google Fonts (the page gets the bundled Archivo instead);
- adds a style sheet before `</head>` that sets the page's colour variables (`--bg`, `--ink`, `--soft`, `--line`,
  `--panel`) to the app's theme, styles `.seg`, `.play`, `.card` and `select` like the app's buttons, and hides the
  page's own title, intro and section links (`header.band h1`, `.lede`, `.nav`).

A new version fits in as long as it keeps those variable and class names; if one is renamed, that part simply keeps
the page's own look. Links to other websites open in the system browser.
