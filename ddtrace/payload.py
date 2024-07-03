from typing import List
from typing import Optional
from typing import TYPE_CHECKING
from typing import Union

from .internal.encoding import Encoder
from .utils.deprecation import RemovedInDDTrace10Warning
from .vendor.debtcollector.removals import removed_property


if TYPE_CHECKING:
    from .span import Span


class PayloadFull(Exception):
    """The payload is full."""

    pass


class Payload(object):
    """
    Trace agent API payload buffer class

    This class is used to encoded and store traces to build the payload we send to
    the trace agent.

    DEV: We encoded and buffer traces so that we can reliable determine the size of
         the payload easily so we can flush based on the payload size.
    """

    __slots__ = ("traces", "size", "_encoder", "max_payload_size")

    # Trace agent limit payload size of 10 MB
    # 5 MB should be a good average efficient size
    DEFAULT_MAX_PAYLOAD_SIZE = 5 * 1000000

    def __init__(
        self,
        encoder=None,  # type: Optional[Encoder]
        max_payload_size=DEFAULT_MAX_PAYLOAD_SIZE,  # type: int
    ):
        # type: (...) -> None
        """
        Constructor for Payload

        :param encoder: The encoded to use, default is the default encoder
        :type encoder: ``ddtrace.internal.encoding.Encoder``
        :param max_payload_size: The max number of bytes a payload should be before
            being considered full (default: 5mb)
        """
        self.max_payload_size = max_payload_size
        self._encoder = encoder or Encoder()
        self.traces = []  # type: List[bytes]
        self.size = 0

    def add_trace(self, trace):
        # type: (Optional[List[Span]]) -> None
        """
        Encode and append a trace to this payload

        :param trace: A trace to append
        :type trace: A list of :class:`ddtrace.span.Span`
        """
        # No trace or empty trace was given, ignore
        if not trace:
            return

        # Encode the trace, append, and add it's length to the size
        encoded = self._encoder.encode_trace(trace)
        if len(encoded) + self.size > self.max_payload_size:
            raise PayloadFull()
        self.traces.append(encoded)
        self.size += len(encoded)

    @removed_property(category=RemovedInDDTrace10Warning)
    def encoder(self):
        # type: () -> Encoder
        return self._encoder

    @encoder.setter  # type: ignore[no-redef]
    def encoder(self, encoder):
        # type: (Encoder) -> None
        self._encoder = encoder

    @property
    def length(self):
        # type: () -> int
        """
        Get the number of traces in this payload

        :returns: The number of traces in the payload
        :rtype: int
        """
        return len(self.traces)

    @property
    def empty(self):
        # type: () -> bool
        """
        Whether this payload is empty or not

        :returns: Whether this payload is empty or not
        :rtype: bool
        """
        return self.length == 0

    def get_payload(self):
        # type: () -> Union[str, bytes]
        """
        Get the fully encoded payload

        :returns: The fully encoded payload
        :rtype: str | bytes
        """
        # DEV: `self.traces` is an array of encoded traces, `join_encoded` joins them together
        return self._encoder.join_encoded(self.traces)

    def __repr__(self):
        # type: () -> str
        """Get the string representation of this payload"""
        return "{0}(length={1}, size={2} B, max_payload_size={3} B)".format(
            self.__class__.__name__, self.length, self.size, self.max_payload_size
        )