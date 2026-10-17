"""Synchronous contextvars API for a single-threaded MicroPython Worker.

Context switching is scoped to Context.run(). It does not automatically track
asyncio Task execution or native threads, which need VM integration.
"""


class _MissingType:
    def __repr__(self):
        return '<Token.MISSING>'


_MISSING = _MissingType()
_active_values = {}
_active_context = None


class Token:
    MISSING = _MISSING

    def __init__(self, var, old_value, context):
        self.var = var
        self.old_value = old_value
        self._context = context
        self._used = False

    def __repr__(self):
        return '<Token var=%r used=%r>' % (self.var, self._used)


class ContextVar:
    def __init__(self, name, *, default=_MISSING):
        if not isinstance(name, str):
            raise TypeError('context variable name must be a str')
        self.name = name
        self._default = default

    def get(self, default=_MISSING):
        if self in _active_values:
            return _active_values[self]
        if default is not _MISSING:
            return default
        if self._default is not _MISSING:
            return self._default
        raise LookupError(self.name)

    def set(self, value):
        old = _active_values.get(self, _MISSING)
        _active_values[self] = value
        return Token(self, old, _active_context)

    def reset(self, token):
        if not isinstance(token, Token) or token.var is not self:
            raise ValueError('Token was created by a different ContextVar')
        if token._used:
            raise RuntimeError('Token has already been used once')
        if token._context is not _active_context:
            raise ValueError('Token was created in a different Context')
        token._used = True
        if token.old_value is _MISSING:
            _active_values.pop(self, None)
        else:
            _active_values[self] = token.old_value

    def __repr__(self):
        return '<ContextVar name=%r>' % self.name


class Context:
    def __init__(self, _values=None):
        self._values = dict(_values or {})
        self._entered = False

    def run(self, callable, *args, **kwargs):
        global _active_context, _active_values
        if self._entered:
            raise RuntimeError('cannot enter context: Context is already entered')
        prior_context = _active_context
        prior_values = _active_values
        self._entered = True
        _active_context = self
        _active_values = self._values
        try:
            return callable(*args, **kwargs)
        finally:
            self._values = _active_values
            _active_context = prior_context
            _active_values = prior_values
            self._entered = False

    def copy(self):
        return Context(self._values)

    def get(self, key, default=None):
        return self._values.get(key, default)

    def keys(self):
        return self._values.keys()

    def values(self):
        return self._values.values()

    def items(self):
        return self._values.items()

    def __getitem__(self, key):
        return self._values[key]

    def __contains__(self, key):
        return key in self._values

    def __iter__(self):
        return iter(self._values)

    def __len__(self):
        return len(self._values)


def copy_context():
    return Context(_active_values)


__all__ = ['Context', 'ContextVar', 'Token', 'copy_context']
