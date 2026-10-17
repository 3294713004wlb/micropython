"""Browser-safe pure-Python CSV reader/writer API compatible with Python 3.10.

Supports multiline quoted fields, dialects, DictReader/DictWriter and common
quoting modes. Parsing is streaming by input line; no native _csv module.
"""

QUOTE_MINIMAL = 0
QUOTE_ALL = 1
QUOTE_NONNUMERIC = 2
QUOTE_NONE = 3


class Error(Exception):
    pass


class Dialect:
    delimiter = ','
    quotechar = '"'
    escapechar = None
    doublequote = True
    skipinitialspace = False
    lineterminator = '\r\n'
    quoting = QUOTE_MINIMAL
    strict = False


class excel(Dialect):
    pass


class excel_tab(excel):
    delimiter = '\t'


class unix_dialect(Dialect):
    lineterminator = '\n'
    quoting = QUOTE_ALL


_registered = {'excel': excel, 'excel-tab': excel_tab, 'unix': unix_dialect}
_field_size_limit = 131072


def field_size_limit(new_limit=None):
    global _field_size_limit
    old = _field_size_limit
    if new_limit is not None:
        _field_size_limit = int(new_limit)
    return old


def register_dialect(name, dialect='excel', **kwargs):
    _registered[name] = _create_dialect(dialect, kwargs)


def unregister_dialect(name):
    if name not in _registered:
        raise Error('unknown dialect')
    del _registered[name]


def get_dialect(name):
    try:
        return _registered[name]
    except KeyError:
        raise Error('unknown dialect')


def list_dialects():
    return list(_registered)


def _create_dialect(dialect, options):
    if isinstance(dialect, str):
        dialect = get_dialect(dialect)
    if dialect is None:
        dialect = excel
    obj = Dialect()
    for name in ('delimiter', 'quotechar', 'escapechar', 'doublequote',
                 'skipinitialspace', 'lineterminator', 'quoting', 'strict'):
        setattr(obj, name, options.get(name, getattr(dialect, name)))
    if len(obj.delimiter) != 1:
        raise TypeError('delimiter must be a 1-character string')
    if obj.quotechar is not None and len(obj.quotechar) != 1:
        raise TypeError('quotechar must be a 1-character string')
    if obj.quoting != QUOTE_NONE and obj.quotechar is None:
        raise TypeError('quotechar must be set when quoting is enabled')
    if obj.quoting not in (QUOTE_MINIMAL, QUOTE_ALL, QUOTE_NONNUMERIC, QUOTE_NONE):
        raise TypeError('bad quoting value')
    return obj


class reader:
    def __init__(self, csvfile, dialect='excel', **fmtparams):
        self.dialect = _create_dialect(dialect, fmtparams)
        self._source = iter(csvfile)
        self.line_num = 0

    def __iter__(self):
        return self

    def __next__(self):
        dialect = self.dialect
        fields = []
        chars = []
        in_quotes = False
        quoted = False
        begun = False
        consumed_any = False
        after_quote = False
        escaped = False
        while True:
            try:
                raw = next(self._source)
            except StopIteration:
                if not consumed_any:
                    raise
                if in_quotes and dialect.strict:
                    raise Error('unexpected end of data')
                fields.append(self._finish(chars, quoted))
                return fields
            self.line_num += 1
            if not isinstance(raw, str):
                raise Error('iterator should return strings, not bytes')
            if '\x00' in raw:
                raise Error('line contains NULL byte')
            consumed_any = True
            # Physical input normally contains the line ending. Preserve it
            # inside quoted values, but never as a record terminator.
            i = 0
            while i < len(raw):
                c = raw[i]
                i += 1
                if escaped:
                    chars.append(c)
                    escaped = False
                    begun = True
                    continue
                if dialect.escapechar is not None and c == dialect.escapechar:
                    escaped = True
                    continue
                if in_quotes:
                    if c == dialect.quotechar:
                        if dialect.doublequote and i < len(raw) and raw[i] == c:
                            chars.append(c)
                            i += 1
                        else:
                            in_quotes = False
                            after_quote = True
                    else:
                        chars.append(c)
                    continue
                if c in ('\r', '\n'):
                    if raw[i:] and raw[i:] != '\n':
                        raise Error("new-line character seen in unquoted field")
                    fields.append(self._finish(chars, quoted))
                    return fields
                if c == dialect.delimiter:
                    fields.append(self._finish(chars, quoted))
                    chars = []
                    begun = False
                    quoted = False
                    after_quote = False
                    continue
                if c == dialect.quotechar and not begun:
                    in_quotes = True
                    quoted = True
                    begun = True
                    continue
                if after_quote and dialect.strict:
                    raise Error("delimiter expected after quote")
                after_quote = False
                if dialect.skipinitialspace and not begun and c == ' ':
                    continue
                chars.append(c)
                begun = True
                if len(chars) > _field_size_limit:
                    raise Error('field larger than field limit')
            if not in_quotes:
                fields.append(self._finish(chars, quoted))
                return fields
            # An iterator that strips newlines is valid input: insert a
            # continuation newline only when it was present in the source.

    def _finish(self, chars, quoted):
        result = ''.join(chars)
        if len(result) > _field_size_limit:
            raise Error('field larger than field limit')
        if self.dialect.quoting == QUOTE_NONNUMERIC and not quoted and result != '':
            return float(result)
        return result


class writer:
    def __init__(self, csvfile, dialect='excel', **fmtparams):
        self.dialect = _create_dialect(dialect, fmtparams)
        self._write = csvfile.write

    def _field(self, value):
        dialect = self.dialect
        text = '' if value is None else str(value)
        quote = dialect.quotechar
        must_quote = dialect.quoting == QUOTE_ALL or (
            dialect.quoting == QUOTE_NONNUMERIC and not isinstance(value, (int, float))
        )
        special = dialect.delimiter + '\r\n' + dialect.lineterminator
        if quote is not None:
            special += quote
        if any(ch in special for ch in text):
            must_quote = True
        if dialect.quoting == QUOTE_NONE:
            must_quote = False
            special = dialect.delimiter + '\r\n' + (quote or '')
            if dialect.escapechar is None and any(ch in special for ch in text):
                raise Error('need to escape, but no escapechar set')
            if dialect.escapechar:
                text = ''.join((dialect.escapechar + c if c in special else c) for c in text)
            return text
        if quote and quote in text:
            if dialect.doublequote:
                text = text.replace(quote, quote + quote)
            elif dialect.escapechar:
                text = text.replace(quote, dialect.escapechar + quote)
            else:
                raise Error('need to escape, but no escapechar set')
        if must_quote:
            return quote + text + quote
        return text

    def writerow(self, row):
        result = self.dialect.delimiter.join(self._field(v) for v in row)
        if result == '' and self.dialect.quoting != QUOTE_NONE:
            result = (self.dialect.quotechar or '"') * 2
        return self._write(result + self.dialect.lineterminator)

    def writerows(self, rows):
        for row in rows:
            self.writerow(row)


class DictReader:
    def __init__(self, f, fieldnames=None, restkey=None, restval=None,
                 dialect='excel', *args, **kwds):
        self.reader = reader(f, dialect, **kwds)
        self._fieldnames = list(fieldnames) if fieldnames is not None else None
        self.restkey = restkey
        self.restval = restval

    @property
    def fieldnames(self):
        if self._fieldnames is None:
            self._fieldnames = next(self.reader)
        return self._fieldnames

    @fieldnames.setter
    def fieldnames(self, value):
        self._fieldnames = value

    @property
    def line_num(self):
        return self.reader.line_num

    def __iter__(self):
        return self

    def __next__(self):
        names = self.fieldnames
        row = next(self.reader)
        while row == []:
            row = next(self.reader)
        result = dict(zip(names, row))
        if len(row) > len(names):
            result[self.restkey] = row[len(names):]
        elif len(row) < len(names):
            for name in names[len(row):]:
                result[name] = self.restval
        return result


class DictWriter:
    def __init__(self, f, fieldnames, restval='', extrasaction='raise',
                 dialect='excel', *args, **kwds):
        self.fieldnames = list(fieldnames)
        self.restval = restval
        self.extrasaction = extrasaction.lower()
        if self.extrasaction not in ('raise', 'ignore'):
            raise ValueError('extrasaction must be raise or ignore')
        self.writer = writer(f, dialect, **kwds)

    def writeheader(self):
        return self.writerow(dict(zip(self.fieldnames, self.fieldnames)))

    def writerow(self, rowdict):
        extras = set(rowdict) - set(self.fieldnames)
        if extras and self.extrasaction == 'raise':
            raise ValueError('dict contains fields not in fieldnames: %r' % extras)
        return self.writer.writerow(rowdict.get(name, self.restval) for name in self.fieldnames)

    def writerows(self, rows):
        for row in rows:
            self.writerow(row)


class Sniffer:
    def sniff(self, sample, delimiters=None):
        if delimiters is None:
            delimiters = ',\t;|:'
        lines = [line for line in sample.splitlines() if line]
        if not lines:
            raise Error('Could not determine delimiter')
        scores = [(sum(line.count(c) for line in lines), c) for c in delimiters]
        count, delimiter = max(scores)
        if not count:
            raise Error('Could not determine delimiter')
        return _create_dialect('excel', {'delimiter': delimiter})

    def has_header(self, sample):
        rows = list(reader(sample.splitlines(), self.sniff(sample)))
        if len(rows) < 2:
            return False
        return any(not str(x).replace('.', '', 1).isdigit() and
                   str(y).replace('.', '', 1).isdigit() for x, y in zip(rows[0], rows[1]))


__all__ = [
    'QUOTE_MINIMAL', 'QUOTE_ALL', 'QUOTE_NONNUMERIC', 'QUOTE_NONE', 'Error',
    'Dialect', 'excel', 'excel_tab', 'unix_dialect', 'reader', 'writer',
    'DictReader', 'DictWriter', 'Sniffer', 'field_size_limit',
    'register_dialect', 'unregister_dialect', 'get_dialect', 'list_dialects',
]
