# Rules for the SEO and outreach system

These bind Claude and anyone else running the tools. Read before every session.

## Voice
British spelling, sentence case, no em dashes, no exclamation marks, no hype words (revolutionary, game-changing, seamless). Real node names and real values. `lint_post.py` enforces most of it.

## Truth
1. Every claim, command and number comes from the source or from a run. Check against FACTS.md. A post with an unchecked `verify:` list stays a draft.
2. State limits plainly (young project, no AAX, hardware inputs refused in headless mode). Honest limits earn trust and links.
3. Never compare against another product with a claim we cannot show. No bashing.

## Publishing
1. Source of truth is `posts/*.md`. Never hand-edit `website/blog/*.html`.
2. The original goes live on infinitedevices.org first. Syndicate at least 48 hours later (`publish.py` gate).
3. Every copy carries a canonical link and an "Originally published at" line.
4. Syndication creates drafts. Going live on a third-party site needs Naman's yes in chat, per post.
5. One post a week, alternating track A (product, developer) and track B (creative). Quality over count.

## Outreach (replies, pitches)
1. Nothing is auto-posted. A person writes each reply, one thread at a time.
2. Answer the question first and fully. Infinite is mentioned only when it is a genuine fit, and the reply must still be useful without it.
3. Say "I make Infinite" once. One link at most. Name one honest limit.
4. Never paste a template. Never reply twice in one thread unless asked.
5. Respect each community's self-promotion rules (Reddit 9:1 norm, subreddit rules, HN guidelines). Use the Show HN or showcase channels for announcements.
6. Do not post in other projects' GitHub issues to advertise. Only where someone asked for a tool.
7. Caps: 3 replies a day in total, 1 per site. If a reply is removed or declined, mark it `declined`, learn, and do not retry.
8. Press and blog pitches: personal, short, one email per person, no follow-up beyond one polite nudge after a week. Naman approves each before it is sent.

## Accounts and secrets
1. Naman creates and logs in to every account. Claude never types passwords.
2. Keys live in `~/.config/infinite-seo.env` (DEVTO_API_KEY, HASHNODE_TOKEN, HASHNODE_PUBLICATION_ID, INDEXNOW_KEY). Never in the repo.

## Workflow in this repo
No pushing. Stage explicit paths, one commit per item, no Co-Authored-By lines unless Naman says otherwise. One build at a time. State the ETA and wait for a go before running tests or app launches.
