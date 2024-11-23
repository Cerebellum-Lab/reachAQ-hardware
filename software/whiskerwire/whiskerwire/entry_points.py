from .app import WhiskerWire
import logging

logging.basicConfig(level=logging.DEBUG)

logging.error("WhiskerWire TOP")


def whiskerwire_app():
    logging.error("WhiskerWire")
    app = WhiskerWire()
    app.run()


if __name__ == "__main__":
    whiskerwire_app()
