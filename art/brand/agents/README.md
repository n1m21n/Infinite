# Chat agent logos

The vendors' own logo files, copied unchanged. They are embedded by `tools/design/build_agent_logos.py`
into `src/app/ui/design/AgentLogos.gen.h`, and drawn only by `DrawAvatar` in `src/app/panels/ChatPanel.cpp`.

| File | Vendor file | Source |
|---|---|---|
| claude-spark-clay.svg | Claude Spark - Clay.svg | Anthropic press kit, anthropic.com/press-kit |
| openai-blossom-black.svg | OAI_OpenAI-Blossom_Black.svg | openai.com/brand, "Download logos" |
| openai-blossom-white.svg | OAI_OpenAI-Blossom_White.svg | openai.com/brand, "Download logos" |
| gemini-spark-512.png | gemini_sparkle_4g_512_lt_*.png | the spark gemini.google.com serves (Google's brand kit needs a partner account) |

Rules followed where they are drawn:

- Never recoloured, cropped, stretched or given effects. The Blossom is black on light themes and white on dark.
- Each mark sits alone on a quiet neutral disc, at most 62% of the disc, so it keeps clear space on every side.
- Used only to say which agent a row, picker entry or reply belongs to; never larger than Infinite's own marks.
- Downloaded 2026-11-05. OpenAI's marks usage terms were accepted for this use.
