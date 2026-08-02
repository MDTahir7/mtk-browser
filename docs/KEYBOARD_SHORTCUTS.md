# Keyboard shortcuts

MTK Browser deliberately does **not** reuse Chrome's and Brave's shortcuts. Every
browser action sits on the **Alt layer**, so the Ctrl combinations a web page or
Windows already owns stay untouched.

The in-product reference is `Alt+/`, which opens this same list as a page. That
page and this document are the two places these keys are written down, and the
page is generated from a single table in `BrowserWindow::ShowShortcutsPage()`.

## Navigation

| Keys | Action |
|---|---|
| `Alt+[` | Back |
| `Alt+]` | Forward |
| `Alt+R` | Reload, or stop while the page is loading |

## Tabs

| Keys | Action |
|---|---|
| `Alt+T` | New tab |
| `Alt+Shift+T` | Reopen the tab you just closed |
| `Alt+X` | Close tab |
| `Alt+U` | Duplicate tab |
| `Alt+,` / `Alt+.` | Previous / next tab |
| `Alt+1` … `Alt+8` | Go to that tab |
| `Alt+9` | Go to the last tab |
| `Alt+Shift+P` | Pin or unpin tab |
| `Alt+M` | Mute or unmute tab |

## Windows

| Keys | Action |
|---|---|
| `Alt+W` | New window |
| `Alt+Shift+W` | New private window |

## Page

| Keys | Action |
|---|---|
| `Alt+A` | Select the address bar |
| `Alt+F` | Find on page |
| `Alt+G` / `Alt+Shift+G` | Next / previous match |
| `Alt+P` | Print |
| `Alt+Enter` | Full screen |
| `Alt+=` / `Alt+-` | Zoom in / out |
| `Alt+0` | Reset zoom |

## Library

| Keys | Action |
|---|---|
| `Alt+B` | Save this page as a bookmark |
| `Alt+Shift+B` | Show or hide the bookmarks bar |
| `Alt+O` | Bookmarks |
| `Alt+H` | History |
| `Alt+J` | Downloads |
| `Alt+K` | Saved passwords |
| `Alt+S` | Settings for this site |
| `Alt+Y` | Your filter rules |
| `Alt+/` | This list, in a tab |

## What is left alone, and why

- **Text editing** — `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Ctrl+A`, `Ctrl+Z`,
  `Ctrl+Y`. These belong to Windows and to web page editors. Rebinding them
  would break typing in every text field, which no amount of distinctiveness is
  worth.
- **`Esc`** — still leaves full screen and closes the find bar. `Esc` means
  "cancel" in every application; it is not a browser convention to avoid.
- **Windows' own keys** — `Alt+Tab`, `Alt+Space` and `Alt+F4` are never bound.
- **`Ctrl+Alt+…`** — ignored on purpose. On many keyboard layouts AltGr arrives
  as Ctrl+Alt, so treating it as a shortcut would swallow characters people are
  trying to type.
- **`Ctrl` + mouse wheel** — still zooms. That is Chromium's own handling, not a
  key binding of ours.

## Trade-off recorded

Being different has a cost: anyone arriving from Chrome, Edge or Brave will press
`Ctrl+T` and get nothing. That was an explicit product decision, and the
mitigations are the `Alt+/` page and accelerator hints shown next to every
matching menu entry.

## Implementation notes

- All of this lives in `BrowserWindow::HandleShortcut`, which is reached three
  ways: the window procedure, the address-bar and find-bar subclasses, and
  `BrowserClient::OnPreKeyEvent` for when focus is inside the page.
- Windows answers an unmatched `Alt`+letter with a beep, so `WM_SYSCHAR` is
  swallowed by the window and by both edit subclasses. The window has no menu
  bar, so nothing else needs that message.
- The `Alt+/` page is a `data:text/html` URL. The navigation policy is
  fail-closed for non-`http(s)` schemes, so the tab is created with
  `BrowserClient::ExpectInternalPage()`, a one-shot allowance consumed by that
  single navigation.
