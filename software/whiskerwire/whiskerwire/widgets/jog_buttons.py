from multiprocessing import Value
from textual.widget import Widget
from textual.containers import Horizontal
from .util import to_valid_identifier
from .glitchless_button import GlitchlessButton
from pyjerrycan import AbsOrRel
from functools import partial

# JogButtons class for controlling directional movement with jog buttons (left/right, big/small steps).
# Provides an interface for fine and coarse control, allowing left and right movements.
class JogButtons(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, widget_name: str, move: callable, small_jog_size: int, big_jog_size: int, max_jog_velocity: int, max_jog_acceleration: int, **kwargs):
        """
        Initialize the JogButtons widget with a specified name and optional configurations.
        
        Args:
            widget_name (str): The name of the widget, used for generating a unique ID.
            **kwargs: Additional keyword arguments for widget customization.
        """
        if small_jog_size <= 0:
            raise ValueError("Small jog size must be a positive integer")
        if big_jog_size <= 0:
            raise ValueError("Big jog size must be a positive integer")
        if max_jog_velocity <= 0:
            raise ValueError("Max jog velocity must be a positive integer")
        if max_jog_acceleration <= 0:
            raise ValueError("Max jog acceleration must be a positive integer")
        
        # Initialize the widget with a valid ID derived from the widget name and apply styling classes
        super().__init__(id=to_valid_identifier(widget_name), classes="jog-buttons", **kwargs)

        self.small_jog_size = small_jog_size
        self.big_jog_size = big_jog_size
        self.max_jog_velocity = max_jog_velocity
        self.max_jog_acceleration = max_jog_acceleration
        self.move = partial(move, max_velocity=self.max_jog_velocity, max_acceleration=self.max_jog_acceleration, abs_or_rel=AbsOrRel.RELATIVE)

        # Define jog buttons for directional movement, providing options for big and small steps
        self.jog_left_big_button = GlitchlessButton("<<", id="jog-left-big-button", classes="jog-button", action=self.jog_left_big_action)
        self.jog_left_small_button = GlitchlessButton("<", id="jog-left-small-button", classes="jog-button", action=self.jog_left_small_action)
        self.jog_right_big_button = GlitchlessButton(">>", id="jog-right-big-button", classes="jog-button", action=self.jog_right_big_action)
        self.jog_right_small_button = GlitchlessButton(">", id="jog-right-small-button", classes="jog-button", action=self.jog_right_small_action)

    def compose_jog_buttons(self):
        """
        Compose the jog buttons into a horizontal layout container for organized display.
        
        Returns:
            Horizontal: A container holding the jog buttons, labeled "Jog" for movement control.
        """
        jog_buttons_container = Horizontal(
            self.jog_left_big_button, self.jog_left_small_button, self.jog_right_small_button, self.jog_right_big_button, classes="jog-button-container"
        )
        jog_buttons_container.border_title = "Jog"  # Title for the jog button container
        return jog_buttons_container

    def jog_left_big_action(self):
        self.move(position=-self.big_jog_size)
    
    def jog_left_small_action(self):
        self.move(position=-self.small_jog_size)
    
    def jog_right_big_action(self):
        self.move(position=self.big_jog_size)
    
    def jog_right_small_action(self):
        self.move(position=self.small_jog_size)