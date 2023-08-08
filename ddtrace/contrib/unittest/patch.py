import inspect
import os
import unittest
import time

import ddtrace
from ddtrace import config
from ddtrace.constants import SPAN_KIND
from ddtrace.contrib.unittest.constants import COMPONENT_VALUE
from ddtrace.contrib.unittest.constants import FRAMEWORK
from ddtrace.contrib.unittest.constants import KIND
from ddtrace.ext import SpanTypes
from ddtrace.ext import test
from ddtrace.internal.ci_visibility import CIVisibility as _CIVisibility
from ddtrace.internal.ci_visibility.constants import EVENT_TYPE as _EVENT_TYPE
from ddtrace.internal.ci_visibility.constants import SESSION_ID as _SESSION_ID
from ddtrace.internal.ci_visibility.constants import SESSION_TYPE as _SESSION_TYPE
from ddtrace.internal.constants import COMPONENT
from ddtrace.internal.logger import get_logger
from ddtrace.internal.utils.wrappers import unwrap as _u
from ddtrace.vendor import wrapt


log = get_logger(__name__)

# unittest default settings
config._add(
    "unittest",
    dict(
        _default_service="unittest",
        operation_name=os.getenv("DD_UNITTEST_OPERATION_NAME", default="unittest.test"),
    ),
)


def _set_tracer(tracer):
    setattr(unittest, "_datadog_tracer", tracer)


def _store_span(item, span):
    """Store span at `unittest` instance."""
    setattr(item, "_datadog_span", span)


def _is_test_suite(item):
    if type(item) == unittest.suite.TestSuite and len(item._tests) and type(item._tests[0]) != unittest.suite.TestSuite:
        return True
    return False


def _is_test_module(item):
    if type(item) == unittest.suite.TestSuite and len(item._tests) and _is_test_suite(item._tests[0]):
        return True
    return False


def _extract_span(item):
    """Extract span from `unittest` instance."""
    return getattr(item, "_datadog_span", None)


def _extract_command_name_from_session(item):
    """Extract command name from `unittest` instance"""
    return getattr(item, "progName", None)


def _extract_test_method_name(item):
    """Extract test method name from `unittest` instance."""
    return getattr(item, "_testMethodName", None)


def _extract_session_status(item):
    if hasattr(item, "result") and hasattr(item.result, "errors") and hasattr(item.result, "failures") and hasattr(
            item.result, "skipped"):
        if len(item.result.errors) or len(item.result.failures):
            return test.Status.FAIL.value
        elif item.result.testsRun == len(item.result.skipped):
            return test.Status.SKIP.value
        return test.Status.PASS.value

    return test.Status.FAIL.value


def _extract_suite_name_from_test_method(item):
    item_type = type(item)
    return getattr(item_type, "__name__", None)


def _extract_module_name_from_test_method(item):
    return getattr(item, "__module__", None)


def _extract_test_skip_reason(args):
    return args[1]


def _extract_test_file_name(item):
    return os.path.basename(inspect.getfile(item.__class__))


def _is_unittest_support_enabled():
    return unittest and getattr(unittest, "_datadog_patch", False) and _CIVisibility.enabled


def patch():
    """
    Patched the instrumented methods from unittest
    """
    if getattr(unittest, "_datadog_patch", False):
        return

    if not _CIVisibility.enabled:
        _CIVisibility.enable(config=ddtrace.config.unittest)

    setattr(unittest, "_datadog_patch", True)

    _w = wrapt.wrap_function_wrapper

    _w(unittest, "TextTestResult.addSuccess", add_success_test_wrapper)
    _w(unittest, "TextTestResult.addFailure", add_failure_test_wrapper)
    _w(unittest, "TextTestResult.addError", add_error_test_wrapper)
    _w(unittest, "TextTestResult.addSkip", add_skip_test_wrapper)

    _w(unittest, "TestCase.run", handle_test_wrapper)
    _w(unittest, "TestSuite.run", handle_module_suite_wrapper)
    _w(unittest, "TestProgram.runTests", handle_session_wrapper)


def unpatch():
    if not getattr(unittest, "_datadog_patch", False):
        return

    setattr(unittest, "_datadog_patch", False)

    _u(unittest, "TextTestResult.addSuccess")
    _u(unittest, "TextTestResult.addFailure")
    _u(unittest, "TextTestResult.addError")
    _u(unittest, "TextTestResult.addSkip")
    _u(unittest, "TestCase.run")


def add_success_test_wrapper(func, instance, args, kwargs):
    if _is_unittest_support_enabled() and instance and type(instance) == unittest.runner.TextTestResult and args:
        test_item = args[0]
        span = _extract_span(test_item)
        if span:
            span.set_tag_str(test.STATUS, test.Status.PASS.value)

    return func(*args, **kwargs)


def add_failure_test_wrapper(func, instance, args, kwargs):
    if _is_unittest_support_enabled() and instance and type(instance) == unittest.runner.TextTestResult and args:
        test_item = args[0]
        span = _extract_span(test_item)
        if span:
            span.set_tag_str(test.STATUS, test.Status.FAIL.value)
        if len(args) > 1:
            exc_info = args[1]
            span.set_exc_info(exc_info[0], exc_info[1], exc_info[2])

    return func(*args, **kwargs)


def add_error_test_wrapper(func, instance, args, kwargs):
    if _is_unittest_support_enabled() and instance and type(instance) == unittest.runner.TextTestResult and args:
        test_item = args[0]
        span = _extract_span(test_item)
        if span:
            span.set_tag_str(test.STATUS, test.Status.FAIL.value)

    return func(*args, **kwargs)


def add_skip_test_wrapper(func, instance, args, kwargs):
    result = func(*args, **kwargs)
    if _is_unittest_support_enabled() and instance and type(instance) == unittest.runner.TextTestResult and args:
        test_item = args[0]
        span = _extract_span(test_item)
        if span:
            span.set_tag_str(test.STATUS, test.Status.SKIP.value)
            span.set_tag_str(test.SKIP_REASON, _extract_test_skip_reason(args))

    return result


def handle_test_wrapper(func, instance, args, kwargs):
    if _is_unittest_support_enabled():
        tracer = getattr(unittest, "_datadog_tracer", _CIVisibility._instance.tracer)
        with tracer._start_span(
                ddtrace.config.unittest.operation_name,
                service=_CIVisibility._instance._service,
                resource="unittest.test",
                span_type=SpanTypes.TEST
        ) as span:
            span.set_tag_str(_EVENT_TYPE, SpanTypes.TEST)

            span.set_tag_str(COMPONENT, COMPONENT_VALUE)
            span.set_tag_str(SPAN_KIND, KIND)

            span.set_tag_str(test.FRAMEWORK, FRAMEWORK)
            span.set_tag_str(test.TYPE, SpanTypes.TEST)

            span.set_tag_str(test.NAME, _extract_test_method_name(instance))
            span.set_tag_str(test.SUITE, _extract_suite_name_from_test_method(instance))
            span.set_tag_str(test.MODULE, _extract_module_name_from_test_method(instance))

            span.set_tag_str(test.STATUS, test.Status.FAIL.value)

            _CIVisibility.set_codeowners_of(_extract_test_file_name(instance), span=span)

            _store_span(instance, span)
    result = func(*args, **kwargs)

    return result


def handle_module_suite_wrapper(func, instance, args, kwargs):
    if _is_test_suite(instance):
        test_suite_name = type(instance._tests[0]).__name__
        print(f'Suite is: {test_suite_name}')
    elif _is_test_module(instance):
        test_module_name = type(instance._tests[0]._tests[0]).__module__
        print(f'Module is: {test_module_name}')
    result = func(*args, **kwargs)

    return result


def handle_session_wrapper(func, instance, args, kwargs):
    if _is_unittest_support_enabled():
        tracer = getattr(unittest, "_datadog_tracer", _CIVisibility._instance.tracer)
        test_session_span = tracer.trace("unittest.test_session", service=_CIVisibility._instance._service,
                                         span_type=SpanTypes.TEST, )
        test_session_span.set_tag_str(COMPONENT, COMPONENT_VALUE)
        test_session_span.set_tag_str(SPAN_KIND, KIND)
        test_session_span.set_tag_str(test.FRAMEWORK, FRAMEWORK)
        test_session_span.set_tag_str(_EVENT_TYPE, _SESSION_TYPE)
        test_session_span.set_tag_str(test.COMMAND, _extract_command_name_from_session(instance))
        test_session_span.set_tag_str(_SESSION_ID, str(test_session_span.span_id))
        _store_span(instance, test_session_span)
    try:
        result = func(*args, **kwargs)
    except SystemExit as e:
        if _CIVisibility.enabled:
            log.debug("CI Visibility enabled - finishing unittest test session")
            test_session_span = _extract_span(instance)
            if test_session_span:
                test_session_span.set_tag_str(test.STATUS, _extract_session_status(instance))
                test_session_span.finish()
                _CIVisibility.disable()
        raise e
    return result
