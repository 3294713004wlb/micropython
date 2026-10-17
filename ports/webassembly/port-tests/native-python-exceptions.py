"""Native exceptions and user subclasses must preserve Python 3 semantics."""

class Error(RuntimeError):
    pass

class TransferError(Error):
    pass

class TransferTimeoutError(TransferError):
    pass

class TransferFaultError(TransferError):
    def __init__(self, *args):
        super(TransferFaultError, self).__init__(*args)

class FatalError(RuntimeError):
    def __init__(self, message):
        RuntimeError.__init__(self, message)

for cls in (Error, TransferError, TransferTimeoutError, TransferFaultError, FatalError):
    value = cls("device failure")
    assert isinstance(value, RuntimeError)
    assert value.args == ("device failure",), (cls, value.args)
    assert str(value) == "device failure"

class DynamicSubclass(ValueError):
    def __init__(self, message):
        super().__init__(message)

assert str(DynamicSubclass("bad input")) == "bad input"
print("NATIVE_EXCEPTION_SUBCLASS_OK")
