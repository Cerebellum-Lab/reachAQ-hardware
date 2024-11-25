from textual.app import ComposeResult
from textual.widget import Widget
from textual_plotext import PlotextPlot
from ..utils import to_valid_identifier, get_logger
from ..model.models.driver_models.sensors.sensor_model import SensorModel
from ..model.models.driver_models.microphone.microphone_model import MicrophoneModel

logger = get_logger()


# GraphWidget class for visualizing sensor data as a graph.
# Inherits from Widget and uses PlotextPlot for rendering the graph.
class GraphWidget(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(
        self,
        model: SensorModel | MicrophoneModel,
        marker: str = "braille",
        show_y_ticks: bool = True,
        **kwargs,
    ):
        """
        Initialize the GraphWidget with a data model, optional y-axis limits, and a marker style.

        Args:
            model (SensorModel): The data model containing sensor readings for the graph.
            y_min (float | None): Optional minimum value for the y-axis.
            y_max (float | None): Optional maximum value for the y-axis.
            marker (str): The character style used for plotting data points (default: "braille").
            **kwargs: Additional keyword arguments for widget customization.
        """
        self.model = model

        # Initialize the widget with appropriate styling classes
        super().__init__(classes="graph-widget", **kwargs)

        # Set the graph's display name and unique identifier
        self.graph_name = self.model.name
        self.identifier = to_valid_identifier(self.graph_name)

        # Initialize the plot widget for displaying the graph
        self.plot = PlotextPlot(id=f"{self.identifier}-graph", classes="graph")

        # Configure x-axis settings, disabling tick marks
        self.plot.plt.xticks([], [])
        if not show_y_ticks:
            self.plot.plt.yticks([0.0], ["0.0"])

        # Set the x-axis limit based on the maximum number of data points
        if isinstance(self.model, SensorModel):
            self.plot.plt.xlim(left=0, right=self.model.MAX_DATA_POINTS)
        elif isinstance(self.model, MicrophoneModel):
            self.plot.plt.xticks(
                [0, self.model.FFT_SIZE // 2, self.model.FFT_SIZE],
                ["0", f"{self.model.FFT_SIZE//2}", f"{self.model.FFT_SIZE}"],
            )
            self.plot.plt.xlim(left=0, right=self.model.FFT_SIZE)

        # Set the plotting marker style
        self.marker = marker

        # Update the graph with initial data
        self.update_graph()

    def compose(self) -> ComposeResult:
        """
        Compose the layout of the GraphWidget, including the plot element.

        Yields:
            PlotextPlot: The plot element configured for displaying sensor data.
        """
        self.update_graph()
        yield self.plot

    def update_graph(self):
        """
        Update the graph by clearing previous data and plotting the latest sensor readings.
        """

        # Access the plot object and clear existing data
        plt = self.plot.plt
        plt.clear_data()

        # Plot the current sensor data with the specified marker style
        if isinstance(self.model, SensorModel):
            plt.plot(self.model.sensor_data, marker=self.marker)
        elif isinstance(self.model, MicrophoneModel):
            plt.plot(self.model.fft_data, marker=self.marker)

        # Apply y-axis limits
        plt.ylim(self.model.Y_MIN, self.model.Y_MAX)
