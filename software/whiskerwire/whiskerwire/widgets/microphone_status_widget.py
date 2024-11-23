from .status_widget import StatusWidget
from .graph_widget import GraphWidget
from model.models.driver_models.microphone import MicrophoneModel


class MicrophoneStatusWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget
    
    def __init__(self, model: MicrophoneModel, y_min: float | None = None, y_max: float | None = None, **kwargs):
        """
        Initialize the MicrophoneStatusWidget with a microphone model and optional configurations.
        
        Args:
            model (MicrophoneModel): The data model containing microphone properties.
            **kwargs: Additional keyword arguments for widget customization.
            y_min (float | None): Optional minimum value for the graph's y-axis.
            y_max (float | None): Optional maximum value for the graph's y-axis.
        """
        
        self.model = model
        super().__init__(self.model.name, self.model.instance, **kwargs)
        
        
        # GraphWidget for visualizing sensor data, with optional y-axis bounds
        self.graph = GraphWidget(self.model, y_min, y_max)
        
        self.model.register_on_fft_data_change(self.on_fft_data_change)
        
    
    def on_fft_data_change(self):
        self.refresh(recompose=True)
    
    def compose(self):
        """
        Compose the layout of the MicrophoneStatusWidget, including the name and graph.
        
        Yields:
            Static and other widgets: The title and graph widget.
        """
        yield self.compose_title()  # Microphone title

        # Ensure the plot refreshes to reflect the latest data
        self.graph.plot.refresh()  
        yield self.graph  # Add the graph widget