import pytest

from ddtrace.appsec._iast._utils import _is_python_version_supported
from tests.appsec.appsec_utils import flask_server


@pytest.mark.skipif(not _is_python_version_supported(), reason="Python version not supported by IAST")
def test_iast_span_metrics():
    with flask_server(iast_enabled="true", token=None) as context:
        _, flask_client, pid = context

        response = flask_client.get("/iast-cmdi-vulnerability?filename=path_traversal_test_file.txt")

        assert response.status_code == 200
        assert response.content == b"OK"
    # TODO: move tests/telemetry/conftest.py::test_agent_session into a common conftest
