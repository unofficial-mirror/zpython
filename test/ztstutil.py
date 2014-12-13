import sys

try:
    from __builtin__ import unicode
    s = unicode
except ImportError:
    s = lambda string: (
        str(string, 'utf-8') if type(string) is bytes else str(string)
    )


class Str(object):
    def __init__(self):
        self.i = 0

    def __str__(self):
        self.i += 1
        return str(self.i)


class CStr(Str):
    def __call__(self, s):
        self.i -= int(s)


class NBase(float):
    __slots__ = ("i",)

    def __init__(self):
        self.i = float(1)


class Int(NBase):
    def __long__(self):
        self.i *= 4
        return int(self.i)

    __int__ = __long__


class CInt(Int):
    def __call__(self, i):
        self.i /= i


class Float(NBase):
    def __float__(self):
        self.i *= 2
        return float(self.i)


class CFloat(Float):
    def __call__(self, i):
        self.i /= i + 1


class Array(object):
    def __init__(self):
        self.accesses = []

    def __len__(self):
        self.accesses += ['len:' + s(len(self.accesses) + 1)]
        return len(self.accesses)

    def __getitem__(self, i):
        self.accesses += ['get:' + s(i)]
        return self.accesses[i]


class CArray(Array):
    def __call__(self, a):
        self.accesses += ['set:' + '|'.join((s(i) for i in a))]

try:
    from collections import OrderedDict
except ImportError:
    from collections import MutableMapping
    try:
        from thread import get_ident as _get_ident
    except ImportError:
        from dummy_thread import get_ident as _get_ident

    # From http://hg.python.org/cpython/rev/026ee0057e2d3305f90a9da41daf7c3f9eb1e814
    #
    # Stripped off documentation and comments
    class OrderedDict(dict):
        def __init__(self, *args, **kwds):
            if len(args) > 1:
                raise TypeError('expected at most 1 arguments, got %d' % len(args))
            try:
                self.__root
            except AttributeError:
                self.__root = root = []
                root[:] = [root, root, None]
                self.__map = {}
            self.__update(*args, **kwds)

        def __setitem__(self, key, value, dict_setitem=dict.__setitem__):
            if key not in self:
                root = self.__root
                last = root[0]
                last[1] = root[0] = self.__map[key] = [last, root, key]
            return dict_setitem(self, key, value)

        def __delitem__(self, key, dict_delitem=dict.__delitem__):
            dict_delitem(self, key)
            link_prev, link_next, _ = self.__map.pop(key)
            link_prev[1] = link_next
            link_next[0] = link_prev

        def __iter__(self):
            root = self.__root
            curr = root[1]
            while curr is not root:
                yield curr[2]
                curr = curr[1]

        def __reversed__(self):
            root = self.__root
            curr = root[0]
            while curr is not root:
                yield curr[2]
                curr = curr[0]

        def clear(self):
            root = self.__root
            root[:] = [root, root, None]
            self.__map.clear()
            dict.clear(self)

        def keys(self):
            return list(self)

        def values(self):
            return [self[key] for key in self]

        def items(self):
            return [(key, self[key]) for key in self]

        def iterkeys(self):
            return iter(self)

        def itervalues(self):
            for k in self:
                yield self[k]

        def iteritems(self):
            for k in self:
                yield (k, self[k])

        update = MutableMapping.update

        __update = update

        __marker = object()

        def pop(self, key, default=__marker):
            if key in self:
                result = self[key]
                del self[key]
                return result
            if default is self.__marker:
                raise KeyError(key)
            return default

        def setdefault(self, key, default=None):
            if key in self:
                return self[key]
            self[key] = default
            return default

        def popitem(self, last=True):
            if not self:
                raise KeyError('dictionary is empty')
            key = next(reversed(self) if last else iter(self))
            value = self.pop(key)
            return key, value

        def __repr__(self, _repr_running={}):
            call_key = id(self), _get_ident()
            if call_key in _repr_running:
                return '...'
            _repr_running[call_key] = 1
            try:
                if not self:
                    return '%s()' % (self.__class__.__name__,)
                return '%s(%r)' % (self.__class__.__name__, self.items())
            finally:
                del _repr_running[call_key]

        def __reduce__(self):
            items = [[k, self[k]] for k in self]
            inst_dict = vars(self).copy()
            for k in vars(OrderedDict()):
                inst_dict.pop(k, None)
            if inst_dict:
                return (self.__class__, (items,), inst_dict)
            return self.__class__, (items,)

        def copy(self):
            return self.__class__(self)

        @classmethod
        def fromkeys(cls, iterable, value=None):
            self = cls()
            for key in iterable:
                self[key] = value
            return self


class Hash(object):
    def accappend(self, a):
        if self.acc and self.acc[-1][0] == a:
            self.acc[-1][1] += 1
        else:
            self.acc.append([a, 1])

    def __init__(self):
        self.d = {'a': 'b'} if sys.version_info < (3,) else {b'a': b'b'}
        self.d = OrderedDict(self.d)
        self.acc = []

    def keys(self):
        self.accappend('k')
        return self.d.keys()

    def __getitem__(self, key):
        self.accappend('[' + s(key) + ']')
        if s(key) == 'acc':
            return ';'.join([k[0] + ('*' + s(k[1]) if k[1] > 1 else '')
                             for k in self.acc])
        return self.d.get(key)

    def __delitem__(self, key):
        self.accappend('![' + s(key) + ']')
        self.d.pop(key)

    def __contains__(self, key):
        # Will be used only if I switch from PyMapping_HasKey to 
        # PySequence_Contains
        self.accappend('?[' + s(key) + ']')
        return s(key) == 'acc' or key in self.d

    def __setitem__(self, key, val):
        self.accappend('[' + s(key) + ']=' + s(val))
        self.d[key] = val

    def __iter__(self):
        self.accappend('i')
        return iter(['acc' if sys.version_info < (3,) else b'acc']
                    + list(self.d.keys()))


class EHash(object):
    def __getitem__(self, i):
        raise SystemError()

    # Invalid number of arguments
    def __setitem__(self, i):
        pass

    def __iter__(self):
        raise NotImplementedError()

    def keys(self):
        raise ValueError()


class EStr(object):
    def __str__(self):
        raise NotImplementedError()

    def __call__(self, s):
        raise KeyError()


class EHash2(object):
    def __getitem__(self, i):
        return EStr()

    def __iter__(self):
        return (str(i) for i in range(1))


class EHash3(object):
    def __getitem__(self, i):
        return None

    def __iter__(self):
        return iter([EStr()])


class ENum(float):
    def __long__(self):
        raise IndexError()

    __int__ = __long__

    def __float__(self):
        raise KeyError()

    def __call__(self, i):
        raise ValueError()


class EArray(Array):
    def __len__(self):
        raise NotImplementedError()

    def __call__(self, a):
        raise IndexError()


class UStr(object):
    def __str__(self):
        return 'abc'

    def __del__(self):
        sys.stderr.write('abc\n')
