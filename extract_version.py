"""
PlatformIO pre-build script: injecter version.json's version+build som
FW_VERSION/FW_BUILD compile-time defines, så version.json forbliver den
ENESTE kilde til versionsnumre (CLAUDE.md regel 1) — også for firmwaren
selv, ikke kun dokumentation/commit-beskeder.
"""

import json

Import("env")

with open("version.json") as f:
    v = json.load(f)

env.Append(
    CPPDEFINES=[
        ("FW_VERSION", '\\"%s\\"' % v["version"]),
        ("FW_BUILD", '\\"%s\\"' % v["build"]),
    ]
)
