"""Python 3.10 dataclasses-compatible public API for the browser MicroPython VM.

This module avoids CPython code generation, inspect.signature, and metaclasses.
Class syntax and annotation handling remain limited by the MicroPython VM.
"""


class _MissingType:
    def __repr__(self):
        return 'MISSING'


MISSING = _MissingType()


class _KWOnlyType:
    def __repr__(self):
        return '<dataclasses._KW_ONLY_TYPE object>'


KW_ONLY = _KWOnlyType()


class _InitVarType:
    def __init__(self, typ=None):
        self.type = typ

    def __repr__(self):
        return 'dataclasses.InitVar[%s]' % self.type


class InitVar:
    def __class_getitem__(cls, item):
        return _InitVarType(item)


class FrozenInstanceError(AttributeError):
    pass


class Field:
    """Describe one annotated dataclass field."""

    def __init__(self, default=MISSING, default_factory=MISSING, init=True,
                 repr=True, hash=None, compare=True, metadata=None, kw_only=MISSING):
        self.name = None
        self.type = None
        self.default = default
        self.default_factory = default_factory
        self.init = init
        self.repr = repr
        self.hash = hash
        self.compare = compare
        self.metadata = {} if metadata is None else dict(metadata)
        self.kw_only = kw_only
        self._field_type = 'field'

    def __repr__(self):
        return ('Field(name=%r,type=%r,default=%r,default_factory=%r,init=%r,'
                'repr=%r,hash=%r,compare=%r,metadata=%r,kw_only=%r)') % (
                    self.name, self.type, self.default, self.default_factory,
                    self.init, self.repr, self.hash, self.compare,
                    self.metadata, self.kw_only)


def field(*, default=MISSING, default_factory=MISSING, init=True, repr=True,
          hash=None, compare=True, metadata=None, kw_only=MISSING):
    if default is not MISSING and default_factory is not MISSING:
        raise ValueError('cannot specify both default and default_factory')
    return Field(default, default_factory, init, repr, hash, compare, metadata, kw_only)


def _class_fields(cls):
    result = getattr(cls, '__dataclass_fields__', None)
    if result is None:
        raise TypeError('must be called with a dataclass type or instance')
    return result


def _ordered_fields(cls):
    """Iterate fields in declaration order, independently of dict iteration.

    MicroPython dictionaries do not guarantee CPython's insertion order.
    This sequence also preserves inherited field positions and overrides.
    """
    definitions = _class_fields(cls)
    sequence = getattr(cls, '__dataclass_field_sequence__', tuple(definitions))
    return tuple(definitions[name] for name in sequence)


def fields(cls):
    """Return declared and inherited fields, excluding InitVar/ClassVar."""
    return tuple(f for f in _ordered_fields(cls) if f._field_type == 'field')


def is_dataclass(obj):
    return hasattr(obj, '__dataclass_fields__')


def _annotation_kind(ann):
    if ann is KW_ONLY:
        return 'kw_only'
    if isinstance(ann, _InitVarType) or ann is InitVar:
        return 'initvar'
    # ClassVar is an annotation-only marker. Do not require typing to import.
    text = str(ann)
    if text == 'typing.ClassVar' or text.startswith('typing.ClassVar[') or text.startswith('ClassVar['):
        return 'classvar'
    return 'field'


def _ordered_bases(target):
    """Return bases from oldest to newest on CPython and MicroPython.

    MicroPython exposes ``__bases__`` but not ``__mro__`` for user types.
    A reverse recursive walk preserves overrides without relying on dict order.
    """
    mro = getattr(target, '__mro__', None)
    if mro is not None:
        return tuple(reversed(mro[1:]))

    ordered = []

    def visit(base):
        for parent in reversed(getattr(base, '__bases__', ())):
            visit(parent)
        if base not in ordered:
            ordered.append(base)

    for base in reversed(getattr(target, '__bases__', ())):
        visit(base)
    return ordered


def _collect_fields(target, default_kw_only):
    definitions = {}
    sequence = []
    for base in _ordered_bases(target):
        previous = getattr(base, '__dataclass_fields__', None)
        if previous:
            base_order = getattr(
                base, '__dataclass_field_sequence__', tuple(previous)
            )
            for name in base_order:
                if name not in definitions:
                    sequence.append(name)
                definitions[name] = previous[name]

    # Use the class's own annotations. The MicroPython lexer may omit them;
    # __dataclass_field_order__ is already supplied by ported CLI sources.
    annotations = target.__dict__.get('__annotations__', {})
    order = target.__dict__.get('__dataclass_field_order__', tuple(annotations))
    keyword_only = default_kw_only
    kw_marker_seen = False
    for name in order:
        ann = annotations.get(name, None)
        kind = _annotation_kind(ann)
        if kind == 'kw_only':
            if kw_marker_seen:
                raise TypeError('KW_ONLY specified more than once')
            kw_marker_seen = True
            keyword_only = True
            continue
        value = target.__dict__.get(name, MISSING)
        if kind == 'classvar':
            # The value remains a normal class attribute.
            continue
        if isinstance(value, Field):
            f = value
        else:
            f = Field(default=value)
        if f.kw_only is MISSING:
            f.kw_only = keyword_only
        f.name = name
        f.type = ann
        f._field_type = kind
        if name not in definitions:
            sequence.append(name)
        definitions[name] = f
        if isinstance(value, Field):
            if f.default is MISSING:
                try:
                    delattr(target, name)
                except AttributeError:
                    pass
            else:
                setattr(target, name, f.default)
    return definitions, tuple(sequence)


def dataclass(cls=None, *, init=True, repr=True, eq=True, order=False,
              unsafe_hash=False, frozen=False, match_args=True, kw_only=False,
              slots=False, weakref_slot=False):
    """Decorate a class using Python 3.10 dataclass constructor semantics.

    ``slots`` needs VM-specific class layout support and is rejected explicitly.
    """
    if order and not eq:
        raise ValueError('eq must be true if order is true')
    if slots or weakref_slot:
        raise NotImplementedError('dataclass slots require MicroPython core support')

    def decorate(target):
        definitions, sequence = _collect_fields(target, kw_only)
        target.__dataclass_fields__ = definitions
        target.__dataclass_field_sequence__ = sequence
        target.__dataclass_params__ = (init, repr, eq, order, unsafe_hash, frozen)
        ordered = [definitions[name] for name in sequence]
        actual = [f for f in ordered if f._field_type == 'field']
        init_fields = [f for f in ordered
                       if f.init and f._field_type in ('field', 'initvar')]
        # Dataclasses allow required keyword-only fields after defaulted fields.
        has_default = False
        for f in init_fields:
            if f.kw_only:
                continue
            if f.default is MISSING and f.default_factory is MISSING:
                if has_default:
                    raise TypeError('non-default argument %r follows default argument' % f.name)
            else:
                has_default = True

        if init:
            def __init__(self, *args, **kwargs):
                positional = [f for f in init_fields if not f.kw_only]
                if len(args) > len(positional):
                    raise TypeError('too many positional arguments')
                initvars = []
                positional_index = 0
                for f in init_fields:
                    if not f.kw_only and positional_index < len(args):
                        if f.name in kwargs:
                            raise TypeError('multiple values for %r' % f.name)
                        value = args[positional_index]
                    elif f.name in kwargs:
                        value = kwargs.pop(f.name)
                    elif f.default_factory is not MISSING:
                        value = f.default_factory()
                    elif f.default is not MISSING:
                        value = f.default
                    else:
                        raise TypeError('missing required argument: %r' % f.name)
                    if not f.kw_only:
                        positional_index += 1
                    if f._field_type == 'initvar':
                        initvars.append(value)
                    else:
                        object.__setattr__(self, f.name, value)
                for f in actual:
                    if not f.init:
                        if f.default_factory is not MISSING:
                            object.__setattr__(self, f.name, f.default_factory())
                        elif f.default is not MISSING:
                            object.__setattr__(self, f.name, f.default)
                if kwargs:
                    raise TypeError('unexpected arguments: %s' % ', '.join(kwargs))
                post_init = getattr(self, '__post_init__', None)
                if post_init is not None:
                    post_init(*initvars)
            target.__init__ = __init__

        if repr:
            def __repr__(self):
                parts = [f.name + '=%r' % getattr(self, f.name)
                         for f in actual if f.repr]
                return '%s(%s)' % (type(self).__name__, ', '.join(parts))
            target.__repr__ = __repr__

        comparing = [f for f in actual if f.compare]
        if eq:
            def __eq__(self, other):
                if type(self) is not type(other):
                    return NotImplemented
                return all(getattr(self, f.name) == getattr(other, f.name)
                           for f in comparing)
            target.__eq__ = __eq__

        if order:
            def _key(obj):
                return tuple(getattr(obj, f.name) for f in comparing)

            def __lt__(self, other):
                if type(self) is not type(other):
                    return NotImplemented
                return _key(self) < _key(other)

            def __le__(self, other):
                if type(self) is not type(other):
                    return NotImplemented
                return _key(self) <= _key(other)

            def __gt__(self, other):
                if type(self) is not type(other):
                    return NotImplemented
                return _key(self) > _key(other)

            def __ge__(self, other):
                if type(self) is not type(other):
                    return NotImplemented
                return _key(self) >= _key(other)
            for name, implementation in (
                ('__lt__', __lt__), ('__le__', __le__),
                ('__gt__', __gt__), ('__ge__', __ge__),
            ):
                if name in target.__dict__:
                    raise TypeError('cannot overwrite %s in class %s' % (name, target.__name__))
                setattr(target, name, implementation)

        if frozen:
            def __setattr__(self, name, value):
                raise FrozenInstanceError('cannot assign to field %r' % name)

            def __delattr__(self, name):
                raise FrozenInstanceError('cannot delete field %r' % name)
            target.__setattr__ = __setattr__
            target.__delattr__ = __delattr__

        if unsafe_hash or (eq and frozen):
            hashing = [
                f for f in actual if f.hash is True or (f.hash is None and f.compare)
            ]

            def __hash__(self):
                return hash(tuple(getattr(self, f.name) for f in hashing))
            if '__hash__' in target.__dict__ and unsafe_hash:
                raise TypeError('Cannot overwrite attribute __hash__')
            target.__hash__ = __hash__
        elif eq and '__hash__' not in target.__dict__:
            target.__hash__ = None
        if match_args:
            target.__match_args__ = tuple(f.name for f in init_fields if not f.kw_only and f._field_type == 'field')
        return target

    if cls is None:
        return decorate
    return decorate(cls)


def _convert(obj, factory, container):
    if is_dataclass(obj) and not isinstance(obj, type):
        entries = [(f.name, _convert(getattr(obj, f.name), factory, container))
                   for f in fields(type(obj))]
        if container == 'dict':
            return factory(entries)
        return factory(value for _, value in entries)
    if isinstance(obj, list):
        return [_convert(v, factory, container) for v in obj]
    if isinstance(obj, tuple):
        return tuple(_convert(v, factory, container) for v in obj)
    if isinstance(obj, dict):
        return type(obj)((_convert(k, factory, container),
                          _convert(v, factory, container)) for k, v in obj.items())
    try:
        from copy import deepcopy
        return deepcopy(obj)
    except (ImportError, AttributeError):
        return obj


def asdict(obj, *, dict_factory=dict):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError('asdict() should be called on dataclass instances')
    return _convert(obj, dict_factory, 'dict')


def astuple(obj, *, tuple_factory=tuple):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError('astuple() should be called on dataclass instances')
    return _convert(obj, tuple_factory, 'tuple')


def replace(obj, **changes):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError('replace() should be called on dataclass instances')
    values = {}
    for f in _ordered_fields(type(obj)):
        if f._field_type == 'classvar':
            continue
        if not f.init:
            if f.name in changes:
                raise ValueError('field %s is declared with init=False' % f.name)
            continue
        if f._field_type == 'initvar':
            if f.name in changes:
                values[f.name] = changes.pop(f.name)
            elif f.default is MISSING and f.default_factory is MISSING:
                raise ValueError('InitVar %r must be specified with replace()' % f.name)
            continue
        values[f.name] = changes.pop(f.name, getattr(obj, f.name))
    if changes:
        raise TypeError('unexpected field(s): %s' % ', '.join(changes))
    return type(obj)(**values)


__all__ = ['MISSING', 'KW_ONLY', 'InitVar', 'FrozenInstanceError',
           'Field', 'field', 'fields', 'is_dataclass', 'dataclass',
           'asdict', 'astuple', 'replace']
