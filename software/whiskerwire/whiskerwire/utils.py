import logging
from re import sub


def get_logger():
    return logging.getLogger("WhiskerWire")


def to_valid_identifier(s: str) -> str:
    """
    Convert a string to a valid identifier.

    This function transforms the input string into a format suitable for identifiers by:
    - Converting the string to lowercase.
    - Replacing spaces with hyphens.
    - Removing or replacing any invalid characters (anything other than letters, digits, or hyphens).
    - Stripping leading and trailing hyphens.
    - Reducing multiple consecutive hyphens to a single hyphen.

    Args:
        s (str): The input string to convert.

    Returns:
        str: The transformed string as a valid identifier.
    """
    # Convert the string to lowercase
    s = s.lower()

    # Replace spaces with hyphens
    s = s.replace(" ", "-")

    # Replace any remaining invalid characters with hyphens (retain only letters, digits, and hyphens)
    s = sub(r"[^a-z0-9-]+", "-", s)

    # Remove leading and trailing hyphens
    s = s.strip("-")

    # Replace multiple consecutive hyphens with a single hyphen
    s = sub(r"-+", "-", s)

    return s


def represents_int(s: str):
    """
    Similar to the builtin str.isdigit() method, but acounts for the
    possibility of negative integers - which isdigit() does not.

    Args:
        s (str): The input string to evaluate

    Returns:
        bool: True if the input string represents a valid integer, else False
    """
    try:
        int(s)
    except ValueError:
        return False
    else:
        return True


def is_power_of_two(value: int) -> bool:
    return (value & (value - 1) == 0) and value != 0
