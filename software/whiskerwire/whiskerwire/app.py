import asyncio
import atexit
import logging
import os
import time
import argparse
from collections import OrderedDict
from datetime import datetime
from enum import Enum
from functools import partial

from pyjerrycan import JerryCAN, JerryCANMsg
from textual import on, work
from textual.app import App, ComposeResult
from textual.containers import Container
from textual.widgets import Header, Button
from textual.widgets import TabbedContent, TabPane, Static

from .config import get_settings, load_settings
from .utils import get_logger
from .widgets.glitchless_button import GlitchlessButton

# Application version information
APP_VERSION_MAJOR = 1
APP_VERSION_MINOR = 1
APP_VERSION_PATCH = 0
APP_VERSION = f"{APP_VERSION_MAJOR}.{APP_VERSION_MINOR}.{APP_VERSION_PATCH}"

# Log file directory and structure setup
LOG_DIR = os.path.abspath("logs")  # Base directory for logs
LOG_SUBDIR = datetime.now().strftime("%Y-%m-%d")  # Subdirectory by date
LOG_FILE = f"{datetime.now().strftime('%Y-%m-%d_%H:%M:%S')}.log"  # Log file name with timestamp

LOG_FILE_DIR = f"{LOG_DIR}/{LOG_SUBDIR}"
LOG_FILE_PATH = f"{LOG_FILE_DIR}/{LOG_FILE}"  # Full path for log file

# Ensure log directory and file creation
if not (os.path.exists(LOG_FILE_DIR) and os.path.isdir(LOG_FILE_DIR)):
    os.makedirs(LOG_FILE_DIR)  # Create directory if not exists
open(LOG_FILE_PATH, "w").close()  # Create an empty log file if it doesn't exist

# Logging configuration for output format and file handler
logging.basicConfig(
    level=logging.INFO,  # Set logging level to DEBUG for verbose output
    format="[%(asctime)s][%(name)s][%(levelname)s]: %(message)s",
    handlers=[logging.FileHandler(LOG_FILE_PATH)],  # Write log to the specified file
)

logger = get_logger()
logger.info(f"WhiskerWire v{APP_VERSION}")  # Initial log entry for version

# Import necessary modules for model handling and dashboard creation
from .model.model import Model, PelletModuleModel, MagnetModuleModel
from .module_dashboards.pellet_module_dashboard import PelletModuleDashboard
from .module_dashboards.magnet_module_dashboard import MagnetModuleDashboard


# Enumeration for module connection status
class ConnectionStatus(Enum):
    DISCONNECTED = 0  # Module is not connected
    SEARCHING = 1  # Module is attempting to reconnect
    CONNECTED = 2  # Module is successfully connected


# Color map for connection status indicators
CONNECTION_STATUS_COLOR_MAP = {
    ConnectionStatus.DISCONNECTED: "red",
    ConnectionStatus.SEARCHING: "yellow",
    ConnectionStatus.CONNECTED: "green",
}


# WhiskerWire main application class
class WhiskerWire(App):
    CSS_PATH = "static/styles.tcss"  # Path to CSS file for styling

    def __init__(self, **kwargs):
        parser = argparse.ArgumentParser(description="WhiskerWire Application")
        parser.add_argument(
            "--verbose", "-v", action="store_true", help="Enable verbose logging"
        )
        parser.add_argument("--config", "-c", type=str, help="Path to config file")
        args = parser.parse_args()

        if args.verbose:
            logger.setLevel(logging.DEBUG)

        if args.config:
            load_settings(args.config)

        settings = get_settings()
        self.HEARTBEAT_PERIOD = settings["Heartbeat Period"]

        self.CONNECTION_STATUS_PERIOD = settings["Network"]["Connection Status Period"]
        self.CONNECTION_TIMEOUT = settings["Network"]["Connection Timeout"]
        self.SEARCHING_TIMEOUT = settings["Network"]["Searching Timeout"]

        # Initialize application, set up title with version info
        super().__init__(**kwargs)
        self.title += f" v{APP_VERSION}"

        self.items = []

        # Establish connection with JerryCAN device
        self.jc = JerryCAN()
        if self.jc.Open() != 0:
            raise ConnectionRefusedError(
                "Failed to open JerryCAN socket"
            )  # Fail if unable to connect

        # Initialize main model to manage app data and states
        self.model = Model()

        # Ordered dictionaries to manage dashboards for pellet and magnet modules
        self.pellet_dashboards = OrderedDict()
        self.magnet_dashboards = OrderedDict()

        # Dictionaries to track last message received time and connection status per module
        self.last_rx_time = dict()
        self.connection_status = OrderedDict()

        # Emergency stop (E-STOP) button setup with initial style
        self.e_stop_button = GlitchlessButton(
            "E-STOP",
            variant="error",
            id="e-stop-button",
            action=partial(self.jc.EStop, True),
        )
        self.e_stop_button.active_effect_duration = 0.2

        self.header = Header(show_clock=True)

        # Register a cleanup function to run on application exit
        atexit.register(self.__cleanup)

        # Register callback for changes to the emergency stop state
        self.model._estop_status.register_on_update(self.on_e_stop_change)

        # Internal flag to indicate if background tasks should run
        self._alive = False

    def on_e_stop_change(self):
        # Toggle E-STOP button label and style based on current state
        if self.model.estop_state:
            self.e_stop_button.set_styles("background: green;")
            self.e_stop_button.label = "RELEASE E-STOP"
        else:
            self.e_stop_button.set_styles("background: red;")
            self.e_stop_button.label = "E-STOP"
        self.e_stop_button.recompose()  # Recompose to reflect style changes

    async def JerryCANrx(self):
        # Receive messages from JerryCAN and pass them to the model
        try:
            rx_message = self.jc.ReceiveMessage()  # Retrieve message from JerryCAN
            if isinstance(rx_message, JerryCANMsg):
                # If valid message, process it with the model
                return self.call_from_thread(self.model.process_message, rx_message)
            return None, None  # Return None if message is invalid
        except Exception as e:
            logger.exception(e, exc_info=True)  # Log any exceptions encountered
            return None, None

    def __cleanup(self):
        # Close the JerryCAN connection upon app exit
        self.jc.Close()

    def add_magnet_module(self, model: MagnetModuleModel):
        # Add and initialize a Magnet module dashboard
        self.magnet_dashboards[model.dst_id] = MagnetModuleDashboard(self.jc, model)
        self.last_rx_time[model.dst_id] = time.time()  # Set the last receive time
        self.connection_status[model.dst_id] = ConnectionStatus.CONNECTED
        self.items.append(self.magnet_dashboards[model.dst_id])  # Add to dashboard list

    def add_pellet_module(self, model: PelletModuleModel):
        # Add and initialize a Pellet module dashboard
        self.pellet_dashboards[model.dst_id] = PelletModuleDashboard(self.jc, model)
        self.last_rx_time[model.dst_id] = time.time()  # Set the last receive time
        self.connection_status[model.dst_id] = ConnectionStatus.CONNECTED
        self.items.append(self.pellet_dashboards[model.dst_id])  # Add to dashboard list

    @work(exclusive=False, thread=True)
    async def heartbeat(self) -> None:
        # Background task to send periodic heartbeat messages to connected modules
        while self._alive:
            await asyncio.sleep(self.HEARTBEAT_PERIOD)  # Sleep before next heartbeat
            if any(
                status == ConnectionStatus.CONNECTED
                for status in self.connection_status.values()
            ):
                self.jc.Heartbeat()  # Send heartbeat if any module is connected

    @work(exclusive=False, thread=True)
    async def watch_connection_status(self) -> None:
        # Background task to monitor and update module connection statuses
        while self._alive:
            await asyncio.sleep(self.CONNECTION_STATUS_PERIOD)
            for dst_id, last_rx_time in self.last_rx_time.items():
                duration = (
                    time.time() - last_rx_time
                )  # Calculate time since last message
                # Update status based on time since last message received
                if (
                    duration >= self.CONNECTION_TIMEOUT
                    and self.connection_status[dst_id] == ConnectionStatus.CONNECTED
                ):
                    self.connection_status[dst_id] = ConnectionStatus.SEARCHING
                    self.call_from_thread(self.recompose)
                elif (
                    duration >= self.SEARCHING_TIMEOUT
                    and self.connection_status[dst_id] == ConnectionStatus.SEARCHING
                ):
                    self.connection_status[dst_id] = ConnectionStatus.DISCONNECTED
                    self.call_from_thread(self.recompose)
                elif (
                    duration < self.CONNECTION_TIMEOUT
                    and self.connection_status[dst_id] != ConnectionStatus.CONNECTED
                ):
                    self.connection_status[dst_id] = ConnectionStatus.CONNECTED
                    self.call_from_thread(self.recompose)

    @work(exclusive=False, thread=True)
    async def update_model(self) -> None:
        # Background task to handle incoming messages and update model
        while self._alive:
            new_module_flag, updated_module_model = await self.JerryCANrx()
            if new_module_flag is not None and updated_module_model is not None:
                # Add new module dashboard if indicated, otherwise refresh existing
                if new_module_flag:
                    if isinstance(updated_module_model, PelletModuleModel):
                        self.call_from_thread(
                            self.add_pellet_module, updated_module_model
                        )
                    elif isinstance(updated_module_model, MagnetModuleModel):
                        self.call_from_thread(
                            self.add_magnet_module, updated_module_model
                        )
                    self.call_from_thread(self.recompose)
                # else:
                # Refresh dashboard for existing module
                #    dashboard = (
                #        self.pellet_dashboards if isinstance(updated_module_model, PelletModuleModel) else self.magnet_dashboards
                #    )[updated_module_model.dst_id]

                # Update last receive time for the module
                self.last_rx_time[updated_module_model.dst_id] = time.time()

    def compose(self) -> ComposeResult:
        # Compose the UI layout with header and dashboard items
        yield self.header
        if self.pellet_dashboards or self.magnet_dashboards:
            # Add the TabbedContent widget
            with TabbedContent(id="module-tabs"):
                for pellet_dashboard in self.pellet_dashboards.values():
                    connection_status = self.connection_status[
                        pellet_dashboard.model.dst_id
                    ]
                    connection_status_indicator = f"[{CONNECTION_STATUS_COLOR_MAP[connection_status]}]•[/{CONNECTION_STATUS_COLOR_MAP[connection_status]}]"
                    with TabPane(
                        title=f"[bold]Pellet Module [{str(pellet_dashboard.model.can_address)}] {connection_status_indicator} [/bold]",
                        id=f"pellet-{pellet_dashboard.model.can_address}",
                    ):
                        yield pellet_dashboard.compose_dashboard()
                for magnet_dashboard in self.magnet_dashboards.values():
                    connection_status = self.connection_status[
                        magnet_dashboard.model.dst_id
                    ]
                    connection_status_indicator = f"[{CONNECTION_STATUS_COLOR_MAP[connection_status]}]•[/{CONNECTION_STATUS_COLOR_MAP[connection_status]}]"
                    with TabPane(
                        title=f"[bold]Magnet Module [{str(magnet_dashboard.model.can_address)}] {connection_status_indicator} [/bold]",
                        id=f"magnet-{magnet_dashboard.model.can_address}",
                    ):
                        yield magnet_dashboard.compose_dashboard()
            # Add the E-Stop button
            yield self.e_stop_button
        else:
            # Add the TabbedContent widget
            with TabbedContent(initial="listening", id="module-tabs"):
                with TabPane(title=f"Listening...", id=f"listening"):
                    yield Container(
                        Static(
                            "Listening for MouseGYM modules...\n(Make sure the can0 interface has been brought up)",
                            id="listening-message",
                        )
                    )

    @on(Button.Pressed, "#e-stop-button")
    async def on_button_press(self, event):
        # Handle E-STOP button press, toggling E-STOP state
        logger.debug(f"E-STOP Button Pressed: State={self.model.estop_state}")
        self.jc.EStop(not self.model.estop_state)  # Send E-STOP command

    async def on_click(self, event) -> None:
        # Ensure only one widget is selected at a time
        for module_dashboard in self.items:
            for dashboard in module_dashboard.dashboards:
                for item in dashboard.items:
                    if item.selected and not item.region.contains(
                        event.screen_x, event.screen_y
                    ):
                        item.on_click(event)
                        item.refresh(recompose=True)
                        return

    def get_active_pane(self):
        try:
            self.get_child_by_type(TabbedContent).active_pane
        except:
            pass

    async def on_mount(self):
        # Start background tasks on app mount
        self._alive = True
        self.run_worker(self.heartbeat, exclusive=False, thread=True)
        self.run_worker(self.update_model, exclusive=False, thread=True)
        self.run_worker(self.watch_connection_status, exclusive=False, thread=True)

    def on_unmount(self):
        # Stop all background tasks when app unmounts
        self._alive = False


# Run the WhiskerWire application
if __name__ == "__main__":
    WhiskerWire(watch_css=True).run()
