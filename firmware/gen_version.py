import re
import subprocess

# Use git describe to get the version
cmd = subprocess.run(
    [
        "git",
        "describe",
        "--tags",
        "--always", "--dirty"
    ],
    capture_output=True,
    check=True)
git_descibe = cmd.stdout.decode('utf-8').strip()

major = 0
minor = 0
patch = 0
tweak = 0
extraversion = ""

# Use a regular expression to extract the version number
# Example match: v1.1.1-6-g59ac656-dirty
m1 = re.match(r'v(\d+)\.(\d+)\.(\d+)(.*)', git_descibe)
if m1:
    major, minor, patch, more_info = m1.groups()

    if more_info:
        m2 = re.match(r'-(\d+)-(.*)', more_info)
        if m2:
            tweak, extraversion = m2.groups()
            extraversion = extraversion.replace('-', '')

print(f"VERSION_MAJOR = {major}")
print(f"VERSION_MINOR = {minor}")
print(f"PATCHLEVEL = {patch}")
print(f"VERSION_TWEAK = {tweak}")
print(f"EXTRAVERSION = {extraversion}")
