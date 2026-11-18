# Prompt: publish the Infinite manuals as one long, linked, SEO-complete HTML page

Work in a new worktree off main (branch `docs/manual-web`). Do not push. Commit per stage with plain
messages (no "Generated with Claude Code", no Co-Authored-By). Python for edits, never BSD sed -i.
Never edit website/embeds/lilliputian_surreal.html. Follow docs/brand/BRAND.md (palette, type, voice).

## Goal
Publish the content of the three manuals as web pages on infinitedevices.org:
- Infinite_Node_Reference_Manual.pdf  -> /manual/            (one long page, plus one page per chapter if it is over ~1.5 MB)
- Infinite_Headless_User_Guide.pdf    -> /manual/headless/
- Field Language manual               -> /manual/field/
The PDFs stay as downloads; the HTML becomes the primary, searchable, linkable version.

## Source of truth (do NOT scrape the PDF text)
1. Read tools/manual/build.py, tools/manual/ui_chapter.py, tools/headless-docs/build.py and the Field manual
   builder. Find the structured data they render from (node tables, markdown, chapter lists, image paths).
2. Write ONE generator, tools/manual/build_html.py, that reads the SAME sources and emits HTML. The PDF and
   HTML must never drift: one content source, two renderers. Wire it into the manual build so both come out
   of one command.

## Page structure
- Semantic HTML: one <h1> per page, <h2> chapters, <h3> sections, <h4> nodes; <nav> table of contents,
  <article>, <section>, <figure>/<figcaption>, real <table> with <th scope>.
- Every heading, node, parameter table and figure gets a stable, human slug id (e.g. #node-field-synth,
  #ch-modulation) and a visible hover anchor link (copy-link). Slugs must be stable across rebuilds; write a
  slug map file and fail the build if an existing slug disappears.
- Sticky TOC (collapsible on mobile), "back to top", previous/next chapter links.
- Works at 375 px with no horizontal scroll; tables scroll inside their own wrapper.

## Linking (internal and external)
- Every node mention in prose links to its section on the manual page AND the node page links back:
  website/nodes/<node>.html "Read in the manual" -> /manual/#node-<slug> (replace the current #page=N PDF links;
  update tools/website/gen_nodes.py so it emits these).
- Cross-links between the three manuals (e.g. Field nodes -> /manual/field/#...; headless commands ->
  /manual/headless/#...).
- Audience pages (artists, musicians, vjs, creative-coders, developers) and the homepage link to the most
  relevant manual sections; add a "Manual" item to the site nav and footer.
- External links: release notes, GitHub repo, npm package, MCP registry, licences of referenced libraries;
  rel="noopener" on target=_blank; no nofollow on our own properties.
- Write a link checker (tools/website/check_links.py): every internal href and #anchor resolves, every
  image exists, external links return < 400 (cache results; run external checks with a timeout and budget).
  Build fails on any broken internal link.

## SEO and metadata
- Per page: unique <title> (<60 chars), meta description (<155 chars), canonical URL, lang, viewport,
  Open Graph (og:title/description/image/url/type=article) and Twitter card, theme-color.
- JSON-LD: TechArticle for each manual page (headline, description, image, author Naman Soni, publisher,
  datePublished/dateModified from git, version 2.0.0), BreadcrumbList, and SoftwareApplication reference.
- Add every new URL to sitemap.xml with lastmod; robots.txt allows them.
- Images: webp with width/height attributes, loading="lazy" (except above the fold), decoding="async",
  descriptive alt written from what is in the picture (never the filename), figcaption in the house caption
  pattern ("Mod Matrix: every modulation in the patch, editable in one table."). Derive sized webps from the
  existing masters via tools/docs-shots/repurpose.py; never upscale.
- Headings and anchors indexed by tools/website/gen_search.py so site search jumps to the exact section.
- Performance: inline critical CSS, no layout shift (CLS < 0.1), page weight budget stated and met.

## Accessibility
- Contrast AA (run the existing website type/contrast audit), visible focus, skip-to-content link,
  tables with captions, figures with alt, anchor links with aria-label.

## Proof (required before you report done)
Write to .proof/ (excluded from git):
1. Screenshots via headless Chrome at 1400 px and 375 px: top of each page, one node section, one table,
   one figure.
2. checks.txt: link checker output (0 broken internal), slug-stability check, HTML validation
   (`python3 -m html5validator` or the W3C nu checker if available), Lighthouse SEO + accessibility scores
   if Chrome supports it (target >= 95), sitemap diff.
3. A table: page | title | description length | canonical | og:image exists | JSON-LD valid.
Do not claim a check passed unless its output is in checks.txt.

## Final report
Short table: file | what changed | proof file. Then anything not done and why.
