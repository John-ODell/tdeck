# Prompt for the website builder

Paste this to the session that builds John's website.

---

Add a project page for **tdeck**, John's custom firmware for the LilyGO T-Deck Plus, to the
website, in the site's existing style and navigation. Everything you need is in the repo
`John-ODell/tdeck` (private; read it with `gh` or a local clone, do not link to it from the
page unless it is public at the time):

- `docs/tdeck.md` is the write-up. It is the source of truth for what the project does and
  how it is built; rewrite it for the page, do not paste it.
- `docs/screenshots/` holds the pictures: `tdeck-*.png` are 320×240 captures from the
  device, `geek-connected.png` is the dongle's 240×135 screen at 2×. Use them as they are,
  on a dark background, scaled with nearest-neighbour or at an integer multiple so the
  pixel font stays crisp. Every image gets alt text that says what the screen shows.
- `README.md` and `dongle/README.md` have the per-app detail and the dongle's protocol if
  you need more than the write-up.

What the page should do:

1. Open with one paragraph a non-technical reader understands: a pocket device that chats
   over LoRa radio with no phone or network, scans the airwaves, knows where it is, and
   doubles as a wireless keyboard and mouse for a computer.
2. Show the launcher screenshot and the dongle screenshot near the top.
3. Walk through the apps in the groups the write-up uses (mesh, radio tools, position,
   keyboard and dongle, everyday), each with one or two screenshots and a short paragraph.
   Keep the green-on-black look of the device in the image frames; the page itself stays
   in the site's own theme.
4. A "how it is built" section for technical readers: the stack list and the four hard
   problems from the write-up, each as problem, cause, fix.
5. A short status line: in daily use on John's own device, in active development, built
   change by change with an issue, pull request and review for each.

Rules:

- Facts come from the repo only. Do not invent features, numbers or dates. If you want a
  count (pull requests, apps, lines), count it with `gh` or `git` and say so in a comment
  in the page source.
- No AI attribution anywhere on the page or in the commit: the work is John's.
- Follow the site's own commit and pull request rules; if it uses the github-bullseye
  recipe, that means an issue, a pull request, a review and a squash merge for this page.
- When the page is up, reply with its URL and a one-line list of which screenshots you
  used.
