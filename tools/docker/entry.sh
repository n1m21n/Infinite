#!/bin/sh
# No arguments: be the MCP server. Otherwise run what was asked (infinite ..., bash, ...).
if [ "$#" -eq 0 ]; then exec infinite-engine serve; fi
exec "$@"
