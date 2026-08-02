# Password manager: threat model and design

This document is written before the feature, because a password store that is
built first and reasoned about afterwards is how password stores go wrong. It
records what MTK Browser protects against, what it does **not** protect against,
and the decisions that follow.

## Why store passwords at all

A family-safe browser that cannot remember a password pushes people to weaker
habits: the same short password everywhere, or a note on the desk. Remembering
them is the safer option, provided the limits below are honest.

## What it protects against

| Threat | Mitigation |
|---|---|
| Another Windows user account on the same PC reading the file | Encryption is user-scoped DPAPI, so a different account cannot decrypt it even with the file |
| Someone copying `passwords.v1` to another machine | DPAPI keys are tied to this user on this machine; the copy is undecryptable |
| A child browsing the saved list and reading a parent's password | Viewing the list and revealing a password require the parent PIN when one is set |
| A page reading a stored password it has no claim to | A credential is only ever filled into the exact origin it was saved for, and only over HTTPS |
| A page asking for every saved password at once | The renderer can request credentials for the origin of the page it is running in, and nothing else |
| Passwords ending up in a plain file by accident | There is no export, no sync and no plaintext file at any point; only the encrypted blob is written |
| A saved password surviving a "clear data" | Clear browsing data can delete saved passwords |

## What it does NOT protect against

Stated plainly, because pretending otherwise is worse than the gap:

- **Malware or another program running as this Windows user.** DPAPI unwraps for
  that user, so any code running as them can decrypt the file. This is the same
  limit Chrome and Edge have on Windows.
- **A person who knows the parent PIN.** The PIN is the gate; there is no second
  factor.
- **A page that already has the credential.** Autofill hands the password to the
  page, which is what filling a form means. If the site is compromised, the
  filled credential is compromised.
- **A physically unlocked, logged-in machine with no PIN set.** Without a PIN the
  list is viewable. The UI says so when no PIN is configured.
- **Phishing on a lookalike domain.** Matching is exact, so a lookalike gets
  nothing filled, but nothing stops a person typing it in by hand.

## Decisions that follow

1. **Encryption**: `CryptProtectData` / `CryptUnprotectData`, user scope, with
   additional entropy that includes the profile id. A blob from one profile
   therefore cannot be decrypted by another profile even inside the same Windows
   account, which keeps profiles as separate as the rest of their data.
   `CRYPTPROTECT_UI_FORBIDDEN` is set: decryption must never raise a prompt.
2. **Storage**: one line per credential in `passwords.v1` inside the profile
   directory, holding the origin, the user name and the hex-encoded blob. Written
   atomically like every other store here. Bounded in count and field length.
3. **Only HTTPS**: nothing is saved for, or filled into, an `http://` origin. A
   password typed over plain HTTP is already exposed, and remembering it would
   quietly encourage repeating that.
4. **Origin match is exact**: scheme and host, the same key the rest of the
   per-site settings use. No subdomain widening, no port folding surprises.
5. **Saving is always a decision**: capture proposes, the person confirms. A
   password is never stored silently.
6. **PIN gates viewing, not filling.** Filling is the everyday case and gating it
   would make the feature useless. Viewing the list, revealing a plaintext
   password and deleting entries require the PIN when one is set.
7. **Plaintext lives as briefly as possible**: decrypted only to fill or to
   reveal, and the buffer is zeroed afterwards.
8. **No export, no sync, no clipboard-all.** A "copy password" action is a single
   credential at a time, and only after the PIN.
9. **The renderer asks about its own origin only.** The browser process derives
   the origin from the frame's URL, never from a value the page sent, so a page
   cannot ask for another site's credential.
10. **A blocked site can hold no credential.** Save requests run through the same
    navigation policy as everything else.

## Reviewing this later

If any of these change, this document changes in the same commit. In particular:
adding sync, adding export, or widening origin matching each invalidate a row in
the table above, and none of them should happen without saying so here.
