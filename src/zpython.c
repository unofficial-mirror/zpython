#include "config.h"
#define MODULE
#include <zsh/zsh.mdh>
#undef MODULE

#include <Python.h>

#if PY_MAJOR_VERSION >= 3
# define PyString_Check             PyBytes_Check
# define PyString_FromString        PyBytes_FromString
# define PyString_FromStringAndSize PyBytes_FromStringAndSize
# define PyString_AsStringAndSize   PyBytes_AsStringAndSize
#endif

#define PYTHON_SAVE_THREAD PyGILState_Release(pygilstate)
#define PYTHON_RESTORE_THREAD pygilstate = PyGILState_Ensure()

struct specialparam {
    char *name;
    Param pm;
    struct specialparam *next;
    struct specialparam *prev;
};

struct special_data {
    struct specialparam *sp;
    PyObject *obj;
};

struct obj_hash_node {
    HashNode next;
    char *nam;
    int flags;
    PyObject *obj;
    struct specialparam *sp;
};

static PyObject *globals;
static zlong zpython_subshell;
static PyObject *hashdict = NULL;
static struct specialparam *first_assigned_param = NULL;
static struct specialparam *last_assigned_param = NULL;
static PyGILState_STATE pygilstate = PyGILState_UNLOCKED;


static void
after_fork()
{
    zpython_subshell = zsh_subshell;
    hashdict = NULL;
    PyOS_AfterFork();
}

#define PYTHON_INIT(failval) \
    PYTHON_RESTORE_THREAD; \
 \
    if (zsh_subshell > zpython_subshell) { \
	after_fork(); \
    }

#if PY_MAJOR_VERSION >= 3
static void
run_flush(PyObject *err)
{
    PyObject *flush, *ret;

    if (!err)
	return;

    if (!(flush = PyObject_GetAttrString(err, "flush"))) {
	PyErr_Clear();
	return;
    }

    if (!(ret = PyObject_CallObject(flush, NULL))) {
	PyErr_Clear();
	Py_DECREF(flush);
	return;
    }

    Py_DECREF(ret);
    Py_DECREF(flush);
}
#endif

static void
flush_io()
{
#if PY_MAJOR_VERSION >= 3
    run_flush(PySys_GetObject("stderr"));
    run_flush(PySys_GetObject("stdout"));
#else
    fflush(stderr);
    fflush(stdout);
#endif
}

#define PYTHON_FINISH \
    flush_io(); \
    PYTHON_SAVE_THREAD

/**/
static int
do_zpython(char *nam, char **args, Options ops, int func)
{
    PyObject *result;
    int exit_code = 0;

    PYTHON_INIT(2);

    result = PyRun_String(*args, Py_file_input, globals, globals);
    if (result == NULL)
    {
	if (PyErr_Occurred()) {
	    PyErr_PrintEx(0);
	    exit_code = 1;
	}
    }
    else
	Py_DECREF(result);
    PyErr_Clear();

    PYTHON_FINISH;
    return exit_code;
}

typedef void *(*Allocator) (size_t);
typedef void (*DeAllocator) (void *, int);

static char *
get_chars(PyObject *string, Allocator alloc)
{
    char *str, *buf, *bufstart;
    Py_ssize_t len = 0;
    Py_ssize_t i = 0;
    Py_ssize_t buflen = 1;

    if (PyString_Check(string)) {
	if (PyString_AsStringAndSize(string, &str, &len) == -1)
	    return NULL;
    }
    else {
#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x03030000
	if (!(str = PyUnicode_AsUTF8AndSize(string, &len)))
	    return NULL;
#else
	PyObject *bytes;
	if (!(bytes = PyUnicode_AsUTF8String(string)))
	    return NULL;

	if (PyString_AsStringAndSize(bytes, &str, &len) == -1) {
	    Py_DECREF(bytes);
	    return NULL;
	}
	Py_DECREF(bytes);
#endif
    }

    while (i < len)
	buflen += 1 + (imeta(str[i++]) ? 1 : 0);

    buf = alloc(buflen * sizeof(char));
    bufstart = buf;

    while (len) {
	if (imeta(*str)) {
	    *buf++ = Meta;
	    *buf++ = *str ^ 32;
	}
	else
	    *buf++ = *str;
	str++;
	len--;
    }
    *buf = '\0';

    return bufstart;
}

static PyObject *
ZshEval(UNUSED(PyObject *self), PyObject *obj)
{
    char *command;

    if (!(command = get_chars(obj, PyMem_Malloc)))
	return NULL;

    execstring(command, 1, 0, ZPYTHON_COMMAND_NAME);

    PyMem_Free(command);

    Py_RETURN_NONE;
}

static PyObject *
get_string(const char *s)
{
    char *buf, *bufstart;
    PyObject *r;
    /* No need in \0 byte at the end since we are using 
     * PyString_FromStringAndSize */
    if (!(buf = PyMem_New(char, strlen(s))))
	return NULL;
    bufstart = buf;
    while (*s) {
	*buf++ = (*s == Meta) ? (*++s ^ 32) : (*s);
	++s;
    }
    r = PyString_FromStringAndSize(bufstart, (Py_ssize_t) (buf - bufstart));
    PyMem_Free(bufstart);
    return r;
}

static void
scanhashdict(HashNode hn, UNUSED(int flags))
{
    struct value v;
    PyObject *key, *val;

    if (hashdict == NULL)
	return;

    v.pm = (Param) hn;

    if (!(key = get_string(v.pm->node.nam))) {
	hashdict = NULL;
	return;
    }

    v.isarr = (PM_TYPE(v.pm->node.flags) & (PM_ARRAY|PM_HASHED));
    v.flags = 0;
    v.start = 0;
    v.end = -1;
    if (!(val = get_string(getstrvalue(&v)))) {
	hashdict = NULL;
	Py_DECREF(key);
	return;
    }

    if (PyDict_SetItem(hashdict, key, val) == -1)
	hashdict = NULL;

    Py_DECREF(key);
    Py_DECREF(val);
}

static PyObject *
get_array(char **ss)
{
    PyObject *r = PyList_New(arrlen(ss));
    size_t i = 0;
    while (*ss) {
	PyObject *str;
	if (!(str = get_string(*ss++))) {
	    Py_DECREF(r);
	    return NULL;
	}
	if (PyList_SetItem(r, i++, str) == -1) {
	    Py_DECREF(r);
	    return NULL;
	}
    }
    return r;
}

static PyObject *
get_hash(HashTable ht)
{
    PyObject *hd;

    if (ht == NULL) {
	hd = PyDict_New();
    } else {
	if (hashdict) {
	    PyErr_SetString(PyExc_RuntimeError, "hashdict already used. "
		    "Do not try to get two hashes simultaneously in "
		    "separate threads, zsh is not thread-safe");
	    return NULL;
	}

	hashdict = PyDict_New();
	if (hashdict == NULL) {
	    return NULL;
	}
	hd = hashdict;

	scanhashtable(ht, 0, 0, 0, scanhashdict, 0);
	if (hashdict == NULL) {
	    Py_DECREF(hd);
	    return NULL;
	}

	hashdict = NULL;
    }
    return hd;
}

static PyObject *
ZshGetValue(UNUSED(PyObject *self), PyObject *args)
{
    char *name;
    struct value vbuf;
    Value v;

    if (!PyArg_ParseTuple(args, "s", &name))
	return NULL;

    if (!isident(name)) {
	PyErr_SetString(PyExc_KeyError, "Parameter name is not an identifier");
	return NULL;
    }

    if (!(v = getvalue(&vbuf, &name, 1))) {
	PyErr_SetString(PyExc_IndexError, "Failed to find parameter");
	return NULL;
    }

    switch (PM_TYPE(v->pm->node.flags)) {
    case PM_HASHED:
	return get_hash(v->pm->gsu.h->getfn(v->pm));
    case PM_ARRAY:
	v->arr = v->pm->gsu.a->getfn(v->pm);
	if (v->isarr) {
	    return get_array(v->arr);
	}
	else {
	    char *s;
	    PyObject *str, *r;

	    if (v->start < 0)
		v->start += arrlen(v->arr);
	    s = (v->start >= arrlen(v->arr) || v->start < 0) ?
		(char *) "" : v->arr[v->start];
	    if (!(str = get_string(s)))
		return NULL;
	    r = PyList_New(1);
	    if (PyList_SetItem(r, 0, str) == -1) {
		Py_DECREF(r);
		return NULL;
	    }
	    return r;
	}
    case PM_INTEGER:
	return PyLong_FromLong((long) v->pm->gsu.i->getfn(v->pm));
    case PM_EFLOAT:
    case PM_FFLOAT:
	return PyFloat_FromDouble(v->pm->gsu.f->getfn(v->pm));
    case PM_SCALAR:
	return get_string(v->pm->gsu.s->getfn(v->pm));
    default:
	PyErr_SetString(PyExc_SystemError,
		"Parameter has unknown type; should not happen.");
	return NULL;
    }
}

static PyObject *
ZshExpand(UNUSED(PyObject *self), PyObject *args)
{
    char *str;
    char *ret;
    int err;
    local_list1(list);

    if (!PyArg_ParseTuple(args, "s", &str))
	return NULL;
    ret = dupstring(str);
    err = parsestrnoerr(ret);
    if (err) {
	if (err > 32 && err < 127)
	    PyErr_Format(PyExc_ValueError, "Parse error near `%c'", err);
	else
	    PyErr_Format(PyExc_ValueError, "Parse error near byte 0x%x", err);
	return NULL;
    }
    singsub(&ret);
    if (errflag) {
	PyErr_SetString(PyExc_RuntimeError, "Expand failed");
	return NULL;
    }
    return get_string(ret);
}

static PyObject *
ZshGlob(UNUSED(PyObject *self), PyObject *args)
{
    char *str, *dup;
    int list_len, i, err;
    local_list1(list);
    LinkNode node, next;
    PyObject *ret;

    if (!PyArg_ParseTuple(args, "s", &str))
	return NULL;
    dup = dupstring(str);
    tokenize(dup);
    init_list1(list, dup);

    zglob(&list, firstnode(&list), 0);
    if (badcshglob == 1) {
	badcshglob = 0;
	PyErr_SetString(PyExc_ValueError, "No match");
	return NULL;
    }
    if (errflag) {
	PyErr_SetString(PyExc_RuntimeError, "Globbing failed");
	return NULL;
    }
    list_len = 0;
    for (node = firstnode(&list); node; node = next) {
	next = nextnode(node);
	list_len++;
    }
    ret = PyList_New(list_len);
    for (i = 0, node = firstnode(&list); i < list_len; node = next, i++) {
	next = nextnode(node);
	PyObject *item = get_string((char *) getdata(node));
	if (item == NULL) {
	    Py_DECREF(ret);
	    return NULL;
	}
	PyList_SET_ITEM(ret, i, item);
    }
    return ret;
}

#define FAIL_SETTING_ARRAY(val, arrlen, dealloc) \
	if (dealloc != NULL) { \
	    while (val-- > valstart) \
		dealloc(*val, strlen(*val)); \
	    dealloc(valstart, arrlen); \
	} \
	return NULL

#define IS_PY_STRING(s) (PyString_Check(s) || PyUnicode_Check(s))

static char **
get_chars_array(PyObject *seq, Allocator alloc, DeAllocator dealloc)
{
    char **val, **valstart;
    Py_ssize_t len = PySequence_Size(seq);
    Py_ssize_t arrlen;
    Py_ssize_t i = 0;

    if (len == -1) {
	PyErr_SetString(PyExc_ValueError, "Failed to get sequence size");
	return NULL;
    }

    arrlen = (len + 1) * sizeof(char *);
    val = (char **) alloc(arrlen);
    valstart = val;

    while (i < len) {
	PyObject *item = PySequence_GetItem(seq, i);

	if (!IS_PY_STRING(item)) {
	    PyErr_SetString(PyExc_TypeError, "Sequence item is not a string");
	    FAIL_SETTING_ARRAY(val, arrlen, dealloc);
	}

	if (!(*val++ = get_chars(item, alloc))) {
	    FAIL_SETTING_ARRAY(val, arrlen, dealloc);
	}
	i++;
    }
    *val = NULL;

    return valstart;
}

static PyObject *
ZshSetValue(UNUSED(PyObject *self), PyObject *args)
{
    char *name;
    PyObject *value;

    if (!PyArg_ParseTuple(args, "sO", &name, &value))
	return NULL;

    if (!isident(name)) {
	PyErr_SetString(PyExc_KeyError, "Parameter name is not an identifier");
	return NULL;
    }

    if (IS_PY_STRING(value)) {
	char *s;

	if (!(s = get_chars(value, zalloc)))
	    return NULL;

	if (!setsparam(name, s)) {
	    PyErr_SetString(PyExc_RuntimeError,
		    "Failed to assign string to the parameter");
	    zsfree(s);
	    return NULL;
	}
    }
#if PY_MAJOR_VERSION < 3
    else if (PyInt_Check(value)) {
	if (!setiparam(name, (zlong) PyInt_AsLong(value))) {
	    PyErr_SetString(PyExc_RuntimeError,
		    "Failed to assign integer parameter");
	    return NULL;
	}
    }
#endif
    else if (PyLong_Check(value)) {
	if (!setiparam(name, (zlong) PyLong_AsLong(value))) {
	    PyErr_SetString(PyExc_RuntimeError,
		    "Failed to assign long parameter");
	    return NULL;
	}
    }
    else if (PyFloat_Check(value)) {
	mnumber mnval;
	mnval.type = MN_FLOAT;
	mnval.u.d = PyFloat_AsDouble(value);
	if (!setnparam(name, mnval)) {
	    PyErr_SetString(PyExc_RuntimeError,
		    "Failed to assign float parameter");
	    return NULL;
	}
    }
    else if (PyDict_Check(value)) {
	char **val, **valstart;
	PyObject *pkey, *pval;
	Py_ssize_t arrlen, pos = 0;

	arrlen = (2 * PyDict_Size(value) + 1) * sizeof(char *);
	val = (char **) zalloc(arrlen);
	valstart = val;

	while (PyDict_Next(value, &pos, &pkey, &pval)) {
	    if (!IS_PY_STRING(pkey)) {
		PyErr_SetString(PyExc_TypeError,
			"Only string keys are allowed");
		FAIL_SETTING_ARRAY(val, arrlen, zfree);
	    }
	    if (!IS_PY_STRING(pval)) {
		PyErr_SetString(PyExc_TypeError,
			"Only string values are allowed");
		FAIL_SETTING_ARRAY(val, arrlen, zfree);
	    }

	    if (!(*val++ = get_chars(pkey, zalloc))) {
		FAIL_SETTING_ARRAY(val, arrlen, zfree);
	    }
	    if (!(*val++ = get_chars(pval, zalloc))) {
		FAIL_SETTING_ARRAY(val, arrlen, zfree);
	    }
	}
	*val = NULL;

	if (!sethparam(name, valstart)) {
	    PyErr_SetString(PyExc_RuntimeError, "Failed to set hash");
	    return NULL;
	}
    }
    /* Python's list have no faster shortcut methods like PyDict_Next above 
     * thus using more abstract protocol */
    else if (PySequence_Check(value)) {
	char **ss = get_chars_array(value, zalloc, zfree);

	if (!ss)
	    return NULL;

	if (!setaparam(name, ss)) {
	    PyErr_SetString(PyExc_RuntimeError, "Failed to set array");
	    return NULL;
	}
    }
    else if (value == Py_None) {
	unsetparam(name);
	if (errflag) {
	    PyErr_SetString(PyExc_RuntimeError, "Failed to delete parameter");
	    return NULL;
	}
    }
    else {
	PyErr_SetString(PyExc_TypeError,
		"Cannot assign value of the given type");
	return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
ZshExitCode(UNUSED(PyObject *self), UNUSED(PyObject *args))
{
    return PyLong_FromLong((long) lastval);
}

static PyObject *
ZshColumns(UNUSED(PyObject *self), UNUSED(PyObject *args))
{
    return PyLong_FromLong((long) zterm_columns);
}

static PyObject *
ZshLines(UNUSED(PyObject *self), UNUSED(PyObject *args))
{
    return PyLong_FromLong((long) zterm_lines);
}

static PyObject *
ZshSubshell(UNUSED(PyObject *self), UNUSED(PyObject *args))
{
    return PyLong_FromLong((long) zsh_subshell);
}

static PyObject *
ZshPipeStatus(UNUSED(PyObject *self), UNUSED(PyObject *args))
{
    size_t i = 0;
    PyObject *r = PyList_New(numpipestats);
    PyObject *num;

    while (i < numpipestats) {
	if (!(num = PyLong_FromLong(pipestats[i]))) {
	    Py_DECREF(r);
	    return NULL;
	}
	if (PyList_SetItem(r, i, num) == -1) {
	    Py_DECREF(r);
	    return NULL;
	}
	i++;
    }
    return r;
}

static void
free_sp(struct specialparam *sp)
{
    if (sp->prev)
	sp->prev->next = sp->next;
    else
	first_assigned_param = sp->next;

    if (sp->next)
	sp->next->prev = sp->prev;
    else
	last_assigned_param = sp->prev;

    zsfree(sp->name);
    PyMem_Free(sp);
}

static void
unset_special_parameter(struct special_data *data)
{
    Py_DECREF(data->obj);
    free_sp(data->sp);
    PyMem_Free(data);
}


#define ZFAIL(errargs, failval) \
    PyErr_PrintEx(0); \
    PYTHON_FINISH; \
    zerr errargs; \
    return failval

#define ZFAIL_NOFINISH(errargs, failval) \
    PyErr_PrintEx(0); \
    flush_io(); \
    zerr errargs; \
    return failval

struct sh_keyobj_data {
    PyObject *obj;
    PyObject *keyobj;
};

struct sh_key_data {
    PyObject *obj;
    char *key;
};

static char *
get_sh_item_value(PyObject *obj, PyObject *keyobj)
{
    PyObject *valobj, *string;
    char *str;

    if (!(valobj = PyObject_GetItem(obj, keyobj))) {
	if (PyErr_Occurred()) {
	    /* Expected result: key not found */
	    if (PyErr_ExceptionMatches(PyExc_KeyError)) {
		PyErr_Clear();
		return dupstring("");
	    }
	    /* Unexpected result: unknown exception */
	    else
		ZFAIL_NOFINISH(("Failed to get value object"), dupstring(""));
	}
	else
	    return dupstring("");
    }

    if (IS_PY_STRING(valobj))
	string = valobj;
    else {
	if (!(string = PyObject_Str(valobj))) {
	    ZFAIL_NOFINISH(("Failed to get value string object"), dupstring(""));
	}
	Py_DECREF(valobj);
    }

    if (!(str = get_chars(string, zhalloc))) {
	Py_DECREF(string);
	ZFAIL_NOFINISH(("Failed to get string from value string object"),
		dupstring(""));
    }
    Py_DECREF(string);
    return str;
}

static char *
get_sh_keyobj_value(Param pm)
{
    struct sh_keyobj_data *sh_kodata = (struct sh_keyobj_data *) pm->u.data;
    return get_sh_item_value(sh_kodata->obj, sh_kodata->keyobj);
}

static char *
get_sh_key_value(Param pm)
{
    char *r, *key;
    PyObject *keyobj, *obj;
    struct sh_key_data *sh_kdata = (struct sh_key_data *) pm->u.data;

    PYTHON_INIT(dupstring(""));

    obj = sh_kdata->obj;
    key = sh_kdata->key;

    if (!(keyobj = get_string(key))) {
	ZFAIL(("Failed to create key string object for key \"%s\"", key),
		dupstring(""));
    }
    r = get_sh_item_value(obj, keyobj);
    Py_DECREF(keyobj);

    PYTHON_FINISH;
    return r;
}

static void
set_sh_item_value(PyObject *obj, PyObject *keyobj, char *val)
{
    PyObject *valobj;

    if (!(valobj = get_string(val))) {
	ZFAIL_NOFINISH(("Failed to create value string object"), );
    }

    if (PyObject_SetItem(obj, keyobj, valobj) == -1) {
	Py_DECREF(valobj);
	ZFAIL_NOFINISH(("Failed to set object to \"%s\"", val), );
    }
    Py_DECREF(valobj);
}

static void
set_sh_key_value(Param pm, char *val)
{
    PyObject *obj, *keyobj;
    char *key;
    struct sh_key_data *sh_kdata = (struct sh_key_data *) pm->u.data;

    PYTHON_INIT();

    obj = sh_kdata->obj;
    key = sh_kdata->key;

    if (!(keyobj = get_string(key))) {
	ZFAIL(("Failed to create key string object for key \"%s\"", key), );
    }
    set_sh_item_value(obj, keyobj, val);
    Py_DECREF(keyobj);

    PYTHON_FINISH;
}

static void
set_sh_keyobj_value(Param pm, char *val)
{
    struct sh_keyobj_data *sh_kodata = (struct sh_keyobj_data *) pm->u.data;
    set_sh_item_value(sh_kodata->obj, sh_kodata->keyobj, val);
}

static struct gsu_scalar sh_keyobj_gsu =
{get_sh_keyobj_value, set_sh_keyobj_value, nullunsetfn};
static struct gsu_scalar sh_key_gsu =
{get_sh_key_value, set_sh_key_value, nullunsetfn};

static HashNode
get_sh_item(HashTable ht, const char *key)
{
    PyObject *obj = ((struct obj_hash_node *) (*ht->nodes))->obj;
    Param pm;
    struct sh_key_data *sh_kdata;

    PYTHON_INIT(NULL);

    pm = (Param) hcalloc(sizeof(struct param));
    pm->node.nam = dupstring(key);
    pm->node.flags = PM_SCALAR;
    pm->gsu.s = &sh_key_gsu;

    sh_kdata = (struct sh_key_data *) hcalloc(sizeof(struct sh_key_data) * 1);
    sh_kdata->obj = obj;
    sh_kdata->key = dupstring(key);

    pm->u.data = (void *) sh_kdata;

    PYTHON_FINISH;

    return &pm->node;
}

static void
scan_special_hash(HashTable ht, ScanFunc func, int flags)
{
    PyObject *obj = ((struct obj_hash_node *) (*ht->nodes))->obj;
    PyObject *iter, *keyobj;
    struct param pm;

    memset((void *) &pm, 0, sizeof(struct param));
    pm.node.flags = PM_SCALAR;
    pm.gsu.s = &sh_keyobj_gsu;

    PYTHON_INIT();

    if (!(iter = PyObject_GetIter(obj))) {
	ZFAIL(("Failed to get iterator"), );
    }

    while ((keyobj = PyIter_Next(iter))) {
	char *str;
	struct sh_keyobj_data sh_kodata;

	if (!IS_PY_STRING(keyobj)) {
	    Py_DECREF(iter);
	    Py_DECREF(keyobj);
	    ZFAIL(("Key is not a string"), );
	}

	if (!(str = get_chars(keyobj, zhalloc))) {
	    Py_DECREF(iter);
	    Py_DECREF(keyobj);
	    ZFAIL(("Failed to get string from key string object"), );
	}
	pm.node.nam = str;

	sh_kodata.obj = obj;
	sh_kodata.keyobj = keyobj;
	pm.u.data = (void *) &sh_kodata;

	func(&pm.node, flags);

	Py_DECREF(keyobj);
    }
    Py_DECREF(iter);

    PYTHON_FINISH;
}

static char *
get_special_string(Param pm)
{
    PyObject *robj;
    char *r;

    PYTHON_INIT(dupstring(""));

    if (!(robj = PyObject_Str(((struct special_data *) pm->u.data)->obj))) {
	ZFAIL(("Failed to create string object for parameter %s",
		    pm->node.nam), dupstring(""));
    }

    if (!(r = get_chars(robj, zhalloc))) {
	ZFAIL(("Failed to transform value for parameter %s", pm->node.nam),
		dupstring(""));
    }

    Py_DECREF(robj);

    PYTHON_FINISH;

    return r;
}

static zlong
get_special_integer(Param pm)
{
    PyObject *robj;
    zlong r;

    PYTHON_INIT(0);

    if (!(robj = PyNumber_Long(((struct special_data *) pm->u.data)->obj))) {
	ZFAIL(("Failed to create int object for parameter %s", pm->node.nam),
		0);
    }

    r = PyLong_AsLong(robj);

    Py_DECREF(robj);

    PYTHON_FINISH;

    return r;
}

static double
get_special_float(Param pm)
{
    PyObject *robj;
    float r;

    PYTHON_INIT(0.0);

    if (!(robj = PyNumber_Float(((struct special_data *) pm->u.data)->obj))) {
	ZFAIL(("Failed to create float object for parameter %s", pm->node.nam),
		0);
    }

    r = PyFloat_AsDouble(robj);

    Py_DECREF(robj);

    PYTHON_FINISH;

    return r;
}

static char **
get_special_array(Param pm)
{
    char **r;

    PYTHON_INIT(hcalloc(sizeof(char **)));

    if (!(r = get_chars_array(((struct special_data *) pm->u.data)->obj,
		    zhalloc, NULL))) {
	ZFAIL(("Failed to create array object for parameter %s", pm->node.nam),
		hcalloc(sizeof(char **)));
    }

    PYTHON_FINISH;

    return r;
}

static void
set_special_string(Param pm, char *val)
{
    PyObject *r, *args;

    PYTHON_INIT();

    if (!val) {
	unset_special_parameter((struct special_data *) pm->u.data);

	PYTHON_FINISH;
	return;
    }

    args = Py_BuildValue("(O&)", get_string, val);
    r = PyObject_CallObject(((struct special_data *) pm->u.data)->obj, args);
    Py_DECREF(args);
    if (!r) {
	PyErr_PrintEx(0);
	zerr("Failed to assign value for string parameter %s", pm->node.nam);
	PYTHON_FINISH;
	return;
    }
    Py_DECREF(r);

    PYTHON_FINISH;
}

static void
set_special_integer(Param pm, zlong val)
{
    PyObject *r, *args;

    PYTHON_INIT();

    args = Py_BuildValue("(L)", (long long) val);
    r = PyObject_CallObject(((struct special_data *) pm->u.data)->obj, args);
    Py_DECREF(args);
    if (!r) {
	PyErr_PrintEx(0);
	zerr("Failed to assign value for integer parameter %s", pm->node.nam);
	PYTHON_FINISH;
	return;
    }
    Py_DECREF(r);

    PYTHON_FINISH;
}

static void
set_special_float(Param pm, double val)
{
    PyObject *r, *args;

    PYTHON_INIT();

    args = Py_BuildValue("(d)", val);
    r = PyObject_CallObject(((struct special_data *) pm->u.data)->obj, args);
    Py_DECREF(args);
    if (!r) {
	PyErr_PrintEx(0);
	zerr("Failed to assign value for float parameter %s", pm->node.nam);
	PYTHON_FINISH;
	return;
    }
    Py_DECREF(r);

    PYTHON_FINISH;
}

static void
set_special_array(Param pm, char **val)
{
    PyObject *r, *args;

    PYTHON_INIT();

    if (!val) {
	unset_special_parameter((struct special_data *) pm->u.data);

	PYTHON_FINISH;
	return;
    }

    args = Py_BuildValue("(O&)", get_array, val);
    r = PyObject_CallObject(((struct special_data *) pm->u.data)->obj, args);
    Py_DECREF(args);
    if (!r) {
	PyErr_PrintEx(0);
	zerr("Failed to assign value for array parameter %s", pm->node.nam);
	PYTHON_FINISH;
	return;
    }
    Py_DECREF(r);

    PYTHON_FINISH;
}

static void
set_special_hash(Param pm, HashTable ht)
{
    int i;
    HashNode hn;
    PyObject *obj = ((struct obj_hash_node *) (*pm->u.hash->nodes))->obj;
    PyObject *keys, *iter, *keyobj;

    if (pm->u.hash == ht)
	return;

    PYTHON_INIT();

    if (!ht) {
	struct specialparam *sp =
	    ((struct obj_hash_node *) (*pm->u.hash->nodes))->sp;
	free_sp(sp);
	Py_DECREF(obj);
	PYTHON_FINISH;
	return;
    }

    /* Can't use PyObject_GetIter on the object itself: it fails if object is 
     * being modified */
    if (!(keys = PyMapping_Keys(obj))) {
	ZFAIL(("Failed to get object keys"), );
    }
    if (!(iter = PyObject_GetIter(keys))) {
	ZFAIL(("Failed to get keys iterator"), );
    }
    while ((keyobj = PyIter_Next(iter))) {
	if (PyMapping_DelItem(obj, keyobj) == -1) {
	    ZFAIL(("Failed to delete some key"), );
	}
	Py_DECREF(keyobj);
    }
    Py_DECREF(iter);
    Py_DECREF(keys);

    for (i = 0; i < ht->hsize; i++)
	for (hn = ht->nodes[i]; hn; hn = hn->next) {
	    struct value v;
	    char *val;
	    PyObject *valobj, *keyobj;

	    v.isarr = v.flags = v.start = 0;
	    v.end = -1;
	    v.arr = NULL;
	    v.pm = (Param) hn;

	    val = getstrvalue(&v);
	    if (!val) {
		ZFAIL(("Failed to get string value"), );
	    }

	    if (!(valobj = get_string(val))) {
		ZFAIL(("Failed to convert value \"%s\" to string object "
			    "while processing key", val, hn->nam), );
	    }
	    if (!(keyobj = get_string(hn->nam))) {
		Py_DECREF(valobj);
		ZFAIL(("Failed to convert key \"%s\" to string object",
			    hn->nam), );
	    }

	    if (PyObject_SetItem(obj, keyobj, valobj) == -1) {
		Py_DECREF(valobj);
		Py_DECREF(keyobj);
		ZFAIL(("Failed to set key %s", hn->nam), );
	    }

	    Py_DECREF(valobj);
	    Py_DECREF(keyobj);
	}
    PYTHON_FINISH;
}

static void
unsetfn(Param pm, int exp)
{
    unset_special_parameter((struct special_data *) pm->u.data);
    stdunsetfn(pm, exp);
}

static void
free_sh_node(HashNode nodeptr)
{
    /* Param is obtained from get_sh_item */
    Param pm = (Param) nodeptr;
    struct sh_key_data *sh_kdata = (struct sh_key_data *) pm->u.data;
    PyObject *keyobj;

    PYTHON_INIT();

    if (!(keyobj = get_string(sh_kdata->key))) {
	ZFAIL(("While unsetting key %s of parameter %s failed to get "
		    "key string object", sh_kdata->key, pm->node.nam), );
    }
    if (PyMapping_DelItem(sh_kdata->obj, keyobj) == -1) {
	Py_DECREF(keyobj);
	ZFAIL(("Failed to delete key %s of parameter %s",
		    sh_kdata->key, pm->node.nam), );
    }
    Py_DECREF(keyobj);

    PYTHON_FINISH;
}

static const struct gsu_scalar special_string_gsu =
{get_special_string, set_special_string, stdunsetfn};
static const struct gsu_integer special_integer_gsu =
{get_special_integer, set_special_integer, unsetfn};
static const struct gsu_float special_float_gsu =
{get_special_float, set_special_float, unsetfn};
static const struct gsu_array special_array_gsu =
{get_special_array, set_special_array, stdunsetfn};
static const struct gsu_hash special_hash_gsu =
{hashgetfn, set_special_hash, stdunsetfn};

static int
check_special_name(char *name)
{
    /* Needing strncasecmp, but the one that ignores locale */
    if (!(         (name[0] == 'z' || name[0] == 'Z')
		&& (name[1] == 'p' || name[1] == 'P')
		&& (name[2] == 'y' || name[2] == 'Y')
		&& (name[3] == 't' || name[3] == 'T')
		&& (name[4] == 'h' || name[4] == 'H')
		&& (name[5] == 'o' || name[5] == 'O')
		&& (name[6] == 'n' || name[6] == 'N')
		&& (name[7] != '\0')
       ) || !isident(name))
    {
	PyErr_SetString(PyExc_KeyError, "Invalid special identifier: "
		"it must be a valid variable name starting with "
		"\"zpython\" (ignoring case) and containing at least one more "
		"character");
	return 1;
    }
    return 0;
}

static PyObject *
set_special_parameter(PyObject *args, int type)
{
    char *name;
    PyObject *obj;
    Param pm;
    int flags = type;
    struct special_data *data;
    struct specialparam *sp;

    if (!PyArg_ParseTuple(args, "sO", &name, &obj))
	return NULL;

    if (check_special_name(name))
	return NULL;

    switch (type) {
    case PM_SCALAR:
	break;
    case PM_INTEGER:
    case PM_EFLOAT:
    case PM_FFLOAT:
	if (!PyNumber_Check(obj)) {
	    PyErr_SetString(PyExc_TypeError,
		    "Object must implement numeric protocol");
	    return NULL;
	}
	break;
    case PM_ARRAY:
	if (!PySequence_Check(obj)) {
	    PyErr_SetString(PyExc_TypeError,
		    "Object must implement sequence protocol");
	    return NULL;
	}
	break;
    case PM_HASHED:
	if (!PyMapping_Check(obj)) {
	    PyErr_SetString(PyExc_TypeError,
		    "Object must implement mapping protocol");
	    return NULL;
	}
	break;
    }

    if (type == PM_HASHED) {
	if (!(pm = createspecialhash(name, get_sh_item,
				    scan_special_hash, flags))) {
	    PyErr_SetString(PyExc_RuntimeError, "Failed to create parameter");
	    return NULL;
	}
    }
    else {
	if (!(pm = createparam(name, flags))) {
	    PyErr_SetString(PyExc_RuntimeError, "Failed to create parameter");
	    return NULL;
	}
    }

    sp = PyMem_New(struct specialparam, 1);
    sp->prev = last_assigned_param;
    if (last_assigned_param)
	last_assigned_param->next = sp;
    else
	first_assigned_param = sp;
    last_assigned_param = sp;
    sp->next = NULL;
    sp->name = ztrdup(name);
    sp->pm = pm;

    if (type != PM_HASHED) {
	data = PyMem_New(struct special_data, 1);
	data->sp = sp;
	data->obj = obj;
	Py_INCREF(obj);
	pm->u.data = data;
    }

    pm->level = 0;

    switch (type) {
    case PM_SCALAR:
	pm->gsu.s = &special_string_gsu;
	break;
    case PM_INTEGER:
	pm->gsu.i = &special_integer_gsu;
	break;
    case PM_EFLOAT:
    case PM_FFLOAT:
	pm->gsu.f = &special_float_gsu;
	break;
    case PM_ARRAY:
	pm->gsu.a = &special_array_gsu;
	break;
    case PM_HASHED:
	{
	    struct obj_hash_node *ohn;
	    HashTable ht = pm->u.hash;
	    ohn = PyMem_New(struct obj_hash_node, 1);
	    ohn->nam = ztrdup("obj");
	    ohn->obj = obj;
	    Py_INCREF(obj);
	    ohn->sp = sp;
	    zfree(ht->nodes, ht->hsize * sizeof(HashNode));
	    ht->nodes = (HashNode *) zshcalloc(1 * sizeof(HashNode));
	    ht->hsize = 1;
	    *ht->nodes = (struct hashnode *) ohn;
	    pm->gsu.h = &special_hash_gsu;
	    ht->freenode = free_sh_node;
	    break;
	}
    }

    Py_RETURN_NONE;
}

static PyObject *
ZshSetMagicString(UNUSED(PyObject *self), PyObject *args)
{
    return set_special_parameter(args, PM_SCALAR);
}

static PyObject *
ZshSetMagicInteger(UNUSED(PyObject *self), PyObject *args)
{
    return set_special_parameter(args, PM_INTEGER);
}

static PyObject *
ZshSetMagicFloat(UNUSED(PyObject *self), PyObject *args)
{
    return set_special_parameter(args, PM_EFLOAT);
}

static PyObject *
ZshSetMagicArray(UNUSED(PyObject *self), PyObject *args)
{
    return set_special_parameter(args, PM_ARRAY);
}

static PyObject *
ZshSetMagicHash(UNUSED(PyObject *self), PyObject *args)
{
    return set_special_parameter(args, PM_HASHED);
}

static struct PyMethodDef ZshMethods[] = {
    {"eval", ZshEval, METH_O,
	"Evaluate command in current shell context",},
    {"last_exit_code", ZshExitCode, METH_NOARGS,
	"Get last exit code. Returns an int"},
    {"pipestatus", ZshPipeStatus, METH_NOARGS,
	"Get last pipe status. Returns a list of int"},
    {"columns", ZshColumns, METH_NOARGS,
	"Get number of columns. Returns an int"},
    {"lines", ZshLines, METH_NOARGS,
	"Get number of lines. Returns an int"},
    {"subshell", ZshSubshell, METH_NOARGS,
	"Get subshell recursion depth. Returns an int"},
    {"getvalue", ZshGetValue, METH_VARARGS,
	"Get parameter value. Return types:\n"
	"  str              for scalars\n"
#if PY_MAJOR_VERSION < 3
	"  long             "
#else
	"  int              "
#endif
	                   "for integer numbers\n"
	"  float            for floating-point numbers\n"
	"  list [str]       for array parameters\n"
	"  dict {str : str} for associative arrays\n"
	"Throws KeyError   if identifier is invalid,\n"
	"       IndexError if parameter was not found\n"
    },
    {"expand", ZshExpand, METH_VARARGS,
	"Perform process substitution, parameter substitution and command substitution on\n"
	"its argument and return the result."},
    {"glob", ZshGlob, METH_VARARGS,
	"Perform globbing on its argument and return the result as a list."},
    {"setvalue", ZshSetValue, METH_VARARGS,
	"Set parameter value. Use None to unset. Supported objects and corresponding\n"
	"zsh parameter types:\n"
	"  str               sets string scalars\n"
#if PY_MAJOR_VERSION < 3
	"  long or int       "
#else
	"  int               "
#endif
	                    "sets integer numbers\n"
	"  float             sets floating-point numbers. Output is in scientific notation\n"
	"  sequence of str   sets array parameters (sequence = anything implementing\n"
	"                    sequence protocol)\n"
	"  dict {str : str}  sets hashes\n"
	"Throws KeyError     if identifier is invalid,\n"
	"       RuntimeError if zsh set?param/unsetparam function failed,\n"
	"       ValueError   if sequence item or dictionary key or value are not str\n"
	"                       or sequence size is not known."},
    {"set_special_string", ZshSetMagicString, METH_VARARGS,
	"Define scalar (string) parameter.\n"
	"First argument is parameter name, it must start with zpython (case is ignored).\n"
	"  Parameter with given name must not exist.\n"
	"Second argument is value object. Its __str__ method will be used to get\n"
	"  resulting string when parameter is accessed in zsh, __call__ method will be used\n"
	"  to set value. If object is not callable then parameter will be considered readonly"},
    {"set_special_integer", ZshSetMagicInteger, METH_VARARGS,
	"Define integer parameter.\n"
	"First argument is parameter name, it must start with zpython (case is ignored).\n"
	"  Parameter with given name must not exist.\n"
	"Second argument is value object. It will be coerced to long integer,\n"
	"  __call__ method will be used to set value. If object is not callable\n"
	"  then parameter will be considered readonly"},
    {"set_special_float", ZshSetMagicFloat, METH_VARARGS,
	"Define floating point parameter.\n"
	"First argument is parameter name, it must start with zpython (case is ignored).\n"
	"  Parameter with given name must not exist.\n"
	"Second argument is value object. It will be coerced to float,\n"
	"  __call__ method will be used to set value. If object is not callable\n"
	"  then parameter will be considered readonly"},
    {"set_special_array", ZshSetMagicArray, METH_VARARGS,
	"Define array parameter.\n"
	"First argument is parameter name, it must start with zpython (case is ignored).\n"
	"  Parameter with given name must not exist.\n"
	"Second argument is value object. It must implement sequence protocol,\n"
	"  each item in sequence must have str type, __call__ method will be used\n"
	"  to set value. If object is not callable then parameter will be\n"
	"  considered readonly"},
    {"set_special_hash", ZshSetMagicHash, METH_VARARGS,
	"Define hash parameter.\n"
	"First argument is parameter name, it must start with zpython (case is ignored).\n"
	"  Parameter with given name must not exist.\n"
	"Second argument is value object. It must implement mapping protocol,\n"
	"  it may be iterable (in this case iterator must return keys),\n"
	"  __getitem__ must be able to work with string objects,\n"
	"  each item must have str type.\n"
	"  __setitem__ will be used to set hash items"},
    {NULL, NULL, 0, NULL},
};

static PyTypeObject EnvironGeneratorType;

typedef PyObject *(*SingleEnvItemGenerator) (char *);

typedef struct {
    PyObject_HEAD
    char **environ;
    SingleEnvItemGenerator getobject;
} EnvironGeneratorObject;

static PyObject *
EnvironGeneratorNext(PyObject *self)
{
    EnvironGeneratorObject *this = (EnvironGeneratorObject *) self;

    if (*this->environ == NULL)
	return NULL;

    return this->getobject(*(this->environ++));
}

static PyObject *
EnvironGeneratorIter(PyObject *self)
{
    Py_INCREF(self);
    return self;
}

static PyObject *
EnvironGeneratorNew(SingleEnvItemGenerator getobject)
{
    EnvironGeneratorObject *self = PyObject_NEW(EnvironGeneratorObject, &EnvironGeneratorType);
    self->environ = environ;
    self->getobject = getobject;
    return (PyObject *) self;
}

static PyObject *
EnvironGetKey(char *eitem)
{
    char *p = strchr(eitem, '=');
    if (p == NULL) {
	PyErr_SetString(PyExc_SystemError, "No = in environ");
	return NULL;
    }
    return PyString_FromStringAndSize(eitem, (Py_ssize_t) (p - eitem));
}

static PyObject *
EnvironGetValue(char *eitem)
{
    char *p = strchr(eitem, '=');
    if (p == NULL) {
	PyErr_SetString(PyExc_SystemError, "No = in environ");
	return NULL;
    }
    return PyString_FromString(p + 1);
}

static PyObject *
EnvironGetItem(char *eitem)
{
    char *p = strchr(eitem, '=');
    if (p == NULL) {
	PyErr_SetString(PyExc_SystemError, "No = in environ");
	return NULL;
    }
    return Py_BuildValue(
#if PY_MAJOR_VERSION < 3
	    "s#s",
#else
	    "y#y",
#endif
	    eitem, (int) (p - eitem), p + 1);
}

static PyTypeObject EnvironType;

static PyObject *
EnvironKeys(UNUSED(PyObject *self))
{
    return EnvironGeneratorNew(EnvironGetKey);
}

static PyObject *
EnvironValues(UNUSED(PyObject *self))
{
    return EnvironGeneratorNew(EnvironGetValue);
}

static PyObject *
EnvironItems(UNUSED(PyObject *self))
{
    return EnvironGeneratorNew(EnvironGetItem);
}

static PyObject *
EnvironCopy(UNUSED(PyObject *self))
{
    char **e;
    PyObject *d = PyDict_New();

    for (e = environ; *e != NULL; e++) {
	PyObject *k;
	PyObject *v;
	char *p = strchr(*e, '=');
	if (p == NULL) {
	    PyErr_SetString(PyExc_SystemError, "No = in environ");
	    Py_DECREF(d);
	    return NULL;
	}
	if (!(k = PyString_FromStringAndSize(*e, (Py_ssize_t) (p - *e)))) {
	    Py_DECREF(d);
	    return NULL;
	}
	if (!(v = PyString_FromString(p + 1))) {
	    Py_DECREF(k);
	    Py_DECREF(d);
	    return NULL;
	}
	if (PyDict_SetItem(d, k, v) != 0) {
	    Py_DECREF(k);
	    Py_DECREF(v);
	    Py_DECREF(d);
	    return NULL;
	}
    }

    return d;
}

static PyObject *
EnvironPop(UNUSED(PyObject *self), PyObject *args)
{
    char *var;
    char *val;
    Param pm;
    PyObject *def = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &var, &def))
	return NULL;

    if (!(val = zgetenv(var))) {
	if (def) {
	    Py_INCREF(def);
	    return def;
	}
	else {
	    PyErr_SetNone(PyExc_KeyError);
	    return NULL;
	}
    }

    unsetparam(var);
    if (errflag) {
	PyErr_SetString(PyExc_RuntimeError, "Failed to delete parameter");
	return NULL;
    }

    return PyString_FromString(val);
}

static PyObject *
EnvironPopItem(PyObject *self, PyObject *args)
{
    PyObject *r;
    char *var;

    if (!PyArg_ParseTuple(args, "s", &var))
	return NULL;

    if (!(r = EnvironPop(self, args)))
	return NULL;

    return Py_BuildValue("sO", var, r);
}

static PyObject *
EnvironGet(UNUSED(PyObject *self), PyObject *args)
{
    char *var;
    char *val;
    Param pm;
    PyObject *def = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &var, &def))
	return NULL;

    if (!(val = zgetenv(var))) {
	if (def) {
	    Py_INCREF(def);
	    return def;
	}
	else {
	    Py_RETURN_NONE;
	}
    }

    return PyString_FromString(val);
}

static char *
get_no_null_chars(PyObject *string)
{
    char *str;

    if (PyString_Check(string)) {
	if (PyString_AsStringAndSize(string, &str, NULL) == -1)
	    return NULL;
    }
    else {
#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x03030000
	if (!(str = PyUnicode_AsUTF8AndSize(string, NULL)))
	    return NULL;
#else
	PyObject *bytes;
	if (!(bytes = PyUnicode_AsUTF8String(string)))
	    return NULL;

	if (PyString_AsStringAndSize(bytes, &str, NULL) == -1) {
	    Py_DECREF(bytes);
	    return NULL;
	}
	Py_DECREF(bytes);
#endif
    }

    return str;
}

static PyMethodDef EnvironMethods[] = {
    {"keys", (PyCFunction) EnvironKeys, METH_NOARGS,
	"Generator of environment variable names, in order they are present in **environ"},
    {"values", (PyCFunction) EnvironValues, METH_NOARGS,
	"Generator of environment variable values, in order they are present in **environ"},
    {"items", (PyCFunction) EnvironItems, METH_NOARGS,
	"Generator of environment variable (name, value) tuples, in order they are present in **environ"},
    {"copy", (PyCFunction) EnvironCopy, METH_NOARGS,
	"Returns a dictionary with the snapshot of current exported environment"},
    {"pop", EnvironPop, METH_VARARGS,
	"Removes and returns exported variable, raising KeyError if it is not available.\n"
	"If second argument is given returns it instead of raising KeyError."},
    {"popitem", EnvironPopItem, METH_VARARGS,
	"Removes exported variable and returns tuple (varname, value), raising KeyError if it is not available"},
    {"get", EnvironGet, METH_VARARGS,
	"Return environment variable value or second argument (defaults to None) if it is not found"},
    {NULL, NULL, 0, NULL},
};

static PyObject *
EnvironItem(UNUSED(PyObject *self), PyObject *keyObject)
{
    char *var;
    char *val;

    if (!(var = get_no_null_chars(keyObject)))
	return NULL;

    if (!(val = zgetenv(var))) {
	PyErr_SetNone(PyExc_KeyError);
	return NULL;
    }

    return PyString_FromString(val);
}

static PyObject *
EnvironAssItem(PyObject *self, PyObject *keyObject, PyObject *valObject)
{
    if (valObject == NULL) {
	PyObject *args;
	PyObject *item;

	if (!(args = PyTuple_Pack(1, keyObject)))
	    return NULL;
	if (!(item = EnvironPop(self, args))) {
	    Py_DECREF(args);
	    return NULL;
	}
	Py_DECREF(args);
	Py_DECREF(item);
	Py_RETURN_NONE;
    }
    else {
	char *var;
	char *val;
	if (!(var = get_no_null_chars(keyObject)))
	    return NULL;

	if (!(val = get_no_null_chars(valObject)))
	    return NULL;

	assignsparam(var, val, PM_EXPORTED);
	Py_RETURN_NONE;
    }
}

static Py_ssize_t
EnvironLength(UNUSED(PyObject *self))
{
    char **e;
    Py_ssize_t r = 0;

    for(e = environ; *e != NULL; e++)
	r++;

    return r;
}

static PyMappingMethods EnvironAsMapping = {
    (lenfunc) EnvironLength,
    (binaryfunc) EnvironItem,
    (objobjargproc) EnvironAssItem,
};

typedef struct {
    PyObject_HEAD
} EnvironObject;

static int
init_types(void)
{
    memset(&EnvironGeneratorType, 0, sizeof(EnvironGeneratorType));
    EnvironGeneratorType.tp_name = "zsh.environ_generator";
    EnvironGeneratorType.tp_basicsize = sizeof(EnvironGeneratorObject);
    EnvironGeneratorType.tp_getattro = PyObject_GenericGetAttr;
    EnvironGeneratorType.tp_iter = EnvironGeneratorIter;
    EnvironGeneratorType.tp_iternext = EnvironGeneratorNext;
    EnvironGeneratorType.tp_flags = Py_TPFLAGS_DEFAULT;

    memset(&EnvironType, 0, sizeof(EnvironType));
    EnvironType.tp_name = "zsh.environ";
    EnvironType.tp_basicsize = sizeof(EnvironObject);
    EnvironType.tp_getattro = PyObject_GenericGetAttr;
    EnvironType.tp_methods = EnvironMethods;
    EnvironType.tp_as_mapping = &EnvironAsMapping;
    EnvironType.tp_flags = Py_TPFLAGS_DEFAULT;

    if (PyType_Ready(&EnvironGeneratorType) == -1)
	return 1;
    if (PyType_Ready(&EnvironType) == -1)
	return 1;
    return 0;
}

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef zshmodule = {
    PyModuleDef_HEAD_INIT,
    "zsh",      /* Module name */
    NULL,       /* Module documentation */
    -1,         /* Size of additional memory needed (no memory needed) */
    ZshMethods, /* Module methods */
    NULL,       /* Unused, should be null. Name: m_reload, type: inquiry */
    NULL,       /* A traversal function to call during GC traversal */
    NULL,       /* A clear function to call during GC clearing */
    NULL,       /* A function to call during deallocation */
};
#endif

static struct builtin bintab[] = {
    BUILTIN(ZPYTHON_COMMAND_NAME, 0, do_zpython,  1, 1, 0, NULL, NULL),
};

static struct features module_features = {
    bintab, sizeof(bintab)/sizeof(*bintab),
    NULL,   0,
    NULL,   0,
    NULL,   0,
    0
};

/**/
int
setup_(UNUSED(Module m))
{
    return 0;
}

/**/
int
features_(Module m, char ***features)
{
    *features = featuresarray(m, &module_features);
    return 0;
}

/**/
int
enables_(Module m, int **enables)
{
    return handlefeatures(m, &module_features, enables);
}

static int
zsh_init_globals(PyObject *zsh_globals)
{
    EnvironObject *environ;

    if (init_types())
	return 1;

    if (!(environ = PyObject_NEW(EnvironObject, &EnvironType)))
	return 1;

    if (PyDict_SetItemString(zsh_globals, "environ", (PyObject *)environ) == -1)
	return 1;
    return 0;
}

static PyObject *
PyInit_zsh()
{
    PyObject *module_globals;
    PyObject *module;

#if PY_MAJOR_VERSION >= 3
    if (!(module = PyModule_Create(&zshmodule)))
	return NULL;
#else
    if (!(module = Py_InitModule("zsh", ZshMethods)))
	return NULL;
#endif
    if (!(module_globals = PyModule_GetDict(module)))
	return NULL;
    if (zsh_init_globals(module_globals))
	return NULL;

    return module;
}

/**/
int
boot_(UNUSED(Module m))
{
#if PY_MAJOR_VERSION >= 3
    size_t zsh_name_size = strlen(argzero);
    wchar_t *zsh_name_wchar;
    wchar_t *argv[2];
    wchar_t *program_name;
    if ((zsh_name_wchar = zhalloc(zsh_name_size + 1)) == NULL)
	return 1;
    mbstowcs(zsh_name_wchar, argzero, zsh_name_size);
    zsh_name_wchar[zsh_name_size] = '\0';
    program_name = zsh_name_wchar;
#else
    char *argv[2];
    char *program_name = argzero;
#endif
    argv[0] = program_name;
    argv[1] = NULL;
    Py_SetProgramName(program_name);
    zpython_subshell = zsh_subshell;
    if (PyImport_AppendInittab("zsh", PyInit_zsh) == -1)
	return 1;
    Py_InitializeEx(0);
    PYTHON_INIT(1);
    PySys_SetArgvEx(1, argv, 0);
    if (!(globals = PyModule_GetDict(PyImport_AddModule("__main__"))))
	return 1;
    PYTHON_FINISH;
    return 0;
}

/**/
int
cleanup_(Module m)
{
    if (Py_IsInitialized()) {
	struct specialparam *cur_sp = first_assigned_param;

	while (cur_sp) {
	    char *name = cur_sp->name;
	    Param pm = (Param) paramtab->getnode(paramtab, name);
	    struct specialparam *next_sp = cur_sp->next;

	    if (pm && pm != cur_sp->pm) {
		Param prevpm, searchpm;
		prevpm = pm;
		searchpm = pm->old;
		while (searchpm && searchpm != cur_sp->pm) {
		    prevpm = searchpm;
		    searchpm = searchpm->old;
		}
		if (searchpm) {
		    paramtab->removenode(paramtab, pm->node.nam);
		    prevpm->old = searchpm->old;
		    searchpm->old = pm;
		    paramtab->addnode(paramtab, searchpm->node.nam, searchpm);

		    pm = searchpm;
		}
		else {
		    pm = NULL;
		}
	    }
	    if (pm) {
		pm->node.flags = (pm->node.flags & ~PM_READONLY) | PM_REMOVABLE;
		unsetparam_pm(pm, 0, 1);
	    }
	    /* Memory was freed while unsetting parameter, thus need to save 
	     * sp->next */
	    cur_sp = next_sp;
	}
	PYTHON_RESTORE_THREAD;
	Py_Finalize();
	pygilstate = PyGILState_UNLOCKED;
    }
    return setfeatureenables(m, &module_features, NULL);
}

/**/
int
finish_(UNUSED(Module m))
{
    return 0;
}

/* vim: ts=8:sts=4:sw=4:noet:
 */
