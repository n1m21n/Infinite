#!/usr/bin/env python3
# Regenerates src/core/McpAgentDoc.h from docs/connect-agent.md.
t = open('docs/connect-agent.md').read()
assert ')RAWDOC' not in t
open('src/core/McpAgentDoc.h', 'w').write('#pragma once\n// Generated from docs/connect-agent.md by tools/gen_agent_doc.py - edit the .md, not this file.\nnamespace McpHttp\n{\n   constexpr const char* kAgentDoc = R"RAWDOC(' + t + ')RAWDOC";\n}\n')
