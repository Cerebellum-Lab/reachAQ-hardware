from os.path import abspath
from json_minify import json_minify
from json import loads

# Config file path
CONFIG_FILE_PATH = abspath("config.jsonc")

try:
    with open(CONFIG_FILE_PATH, "r") as f:
        contents = "".join(f.readlines())
        settings = loads(json_minify(contents))
    with open("test.txt", "a") as f:
        f.write("test")
except:
    exit(1)
