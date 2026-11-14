# PlatformIO pre-build script: puts the git commit into the firmware.
#
# Defines for lib/tableboard/src/version.h:
#   TND_COMMIT      "1a2b3c4d" (8 hex digits) or "unknown"
#   TND_COMMIT_U32  the same 8 hex digits as a number, for the shift log
#   TND_DIRTY       1 if the work tree had changes not in the commit
#   TND_BUILD_DATE  "2026-11-13" (UTC)
import datetime
import subprocess

Import("env")  # noqa: F821 (PlatformIO SCons)


def git(*args):
    try:
        out = subprocess.run(["git", *args], cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


commit = git("rev-parse", "--short=8", "HEAD")
if len(commit) != 8:
    commit = ""
dirty = 1 if commit and git("status", "--porcelain", "--untracked-files=no") else 0
date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")

env.Append(CPPDEFINES=[  # noqa: F821
    ("TND_COMMIT", '\\"%s\\"' % (commit or "unknown")),
    ("TND_COMMIT_U32", "0x%sUL" % (commit or "0")),
    ("TND_DIRTY", dirty),
    ("TND_BUILD_DATE", '\\"%s\\"' % date),
])
print("TND-01 firmware %s%s, %s" % (commit or "unknown", "+dirty" if dirty else "", date))
