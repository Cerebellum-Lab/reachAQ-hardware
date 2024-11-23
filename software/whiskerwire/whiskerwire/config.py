import os
import sys
from json_minify import json_minify
from json import loads

from .utils import get_logger

logger = get_logger()

# Default config file path relative to this file
DEFAULT_CONFIG_FILE_PATH = os.path.join(os.path.dirname(__file__), "config.jsonc")

settings = None


def load_settings(config_file_path: str = DEFAULT_CONFIG_FILE_PATH):
    global settings
    try:
        logger.debug(f"Loading config file from {config_file_path}")
        with open(config_file_path, "r") as f:
            contents = "".join(f.readlines())
            settings = loads(json_minify(contents))
    except Exception as e:
        logger.error(f"Failed to load config file {config_file_path} [{e}]")
        sys.exit(1)


def get_settings():
    global settings
    if settings is None:
        load_settings()
    return settings
