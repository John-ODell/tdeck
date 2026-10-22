# Prints a -DFW_VERSION build flag from git (used by platformio.ini)
import subprocess
try:
    v = subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"], text=True).strip()
except Exception:
    v = "dev"
print('-DFW_VERSION=\\"%s\\"' % v)
