from typing import Optional

from ddtrace import Span
from ddtrace import config
from ddtrace.ext import SpanTypes
from ddtrace.llmobs._constants import ML_APP
from ddtrace.llmobs._constants import SESSION_ID


def _get_nearest_llmobs_ancestor(span: Span) -> Optional[Span]:
    """Return the nearest LLMObs-type ancestor span of a given span."""
    parent = span._parent
    while parent:
        if parent.span_type == SpanTypes.LLM:
            return parent
        parent = parent._parent
    return None


def _get_llmobs_parent_id(span: Span) -> Optional[int]:
    """Return the span ID of the nearest LLMObs-type span in the span's ancestor tree."""
    nearest_llmobs_ancestor = _get_nearest_llmobs_ancestor(span)
    if nearest_llmobs_ancestor:
        return nearest_llmobs_ancestor.span_id
    local_root = span._local_root
    if span is local_root:
        return None
    return local_root.get_tag("_dd.p.llmobs_parent_id")


def _get_ml_app(span: Span) -> str:
    """
    Return the ML app name for a given span, by checking the span's nearest LLMObs span ancestor.
    Default to the global config LLMObs ML app name otherwise.
    """
    ml_app = span.get_tag(ML_APP)
    if ml_app:
        return ml_app
    nearest_llmobs_ancestor = _get_nearest_llmobs_ancestor(span)
    if nearest_llmobs_ancestor:
        ml_app = nearest_llmobs_ancestor.get_tag(ML_APP)
    return ml_app or config._llmobs_ml_app or "uknown-ml-app"


def _get_session_id(span: Span) -> str:
    """
    Return the session ID for a given span, by checking the span's nearest LLMObs span ancestor.
    Default to the span's trace ID.
    """
    session_id = span.get_tag(SESSION_ID)
    if session_id:
        return session_id
    nearest_llmobs_ancestor = _get_nearest_llmobs_ancestor(span)
    if nearest_llmobs_ancestor:
        session_id = nearest_llmobs_ancestor.get_tag(SESSION_ID)
    return session_id or "{:x}".format(span.trace_id)


def _inject_llmobs_parent_id(span_context, span):
    """Inject the LLMObs parent ID into the span context for reconnecting distributed LLMObs traces."""
    if span is None:
        return
    if span.span_type == SpanTypes.LLM:
        llmobs_parent_id = span.span_id
    else:
        llmobs_parent_id = _get_llmobs_parent_id(span)
    if llmobs_parent_id:
        span_context._meta["_dd.p.llmobs_parent_id"] = str(llmobs_parent_id)
