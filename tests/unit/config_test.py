import ctypes
import pytest

def cdll():
    cdll = ctypes.CDLL("../tests.so")
    return cdll

def test_config_set(cdll):
    cdll.wstrNew.restype = ctypes.c_void_p
    wstr = cdll.wstrNew("port 1000")
    pytest.set_trace()
    cdll.applyConfig(wstr)
    result = ctypes.create_string_buffer(30)
    cdll.statConfigByName("port", result, 30)
    assert result.value == '10828'
