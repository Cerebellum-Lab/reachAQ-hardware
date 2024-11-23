from os.path import abspath
from json_minify import json_minify
from json import loads

from .utils import get_logger

logger = get_logger()

# Config file path
CONFIG_FILE_PATH = abspath("config.jsonc")

try:
    logger.debug(f"Loading config file from {CONFIG_FILE_PATH}")
    with open(CONFIG_FILE_PATH, "r") as f:
        contents = "".join(f.readlines())
        settings = loads(json_minify(contents))
except Exception as e:
    logger.error(f"Failed to load config file {CONFIG_FILE_PATH} [{e}]")
    exit(1)
