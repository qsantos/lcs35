#include <Python.h>

#include "square.c"

void mpz_set_py(mpz_t op, PyObject* o) {
    PyObject* pystr = PyObject_Str(o);
#if PY_MAJOR_VERSION >= 3
    PyObject* pybytes = PyUnicode_AsUTF8String(pystr);
    const char* str = PyBytes_AsString(pybytes);
#else
    const char* str = PyString_AsString(pystr);
#endif
    mpz_set_str(op, str, 0);
#if PY_MAJOR_VERSION >= 3
    Py_DECREF(pybytes);
#endif
    Py_DECREF(pystr);
}

PyObject* mpz_get_py(mpz_t op) {
    char* str = mpz_get_str(NULL, 10, op);
#if PY_MAJOR_VERSION >= 3
    //PyObject* pybytes = PyBytes_FromString(str);
    PyObject* ret = PyLong_FromString(str, NULL, 0);
#else
    PyObject* ret = PyLong_FromString(str, NULL, 0);
#endif
    free(str);
    return ret;
}

static PyObject* wrapper_square(PyObject* self, PyObject* args)
{
    (void) self;

    PyObject* X;
    long int t;
    PyObject* N;

    if (!PyArg_ParseTuple(args, "OlO", &X, &t, &N)) {
        return NULL;
    }

    mpz_t x; mpz_init(x); mpz_set_py(x, X);
    mpz_t n; mpz_init(n); mpz_set_py(n, N);

    square(x, t, n);

    PyObject* ret = mpz_get_py(x);
    mpz_clear(n);
    mpz_clear(x);
    return ret;
}

static PyMethodDef methods[] =
{
    {
        "square", wrapper_square, METH_VARARGS,
        "Compute an iterated square",
    },
    {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "Iterated square utility",
    NULL, 0, methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit_square_py3()
{
    return PyModule_Create(&moduledef);
}
#else
PyMODINIT_FUNC initsquare_py2()
{
    Py_InitModule("square_py2", methods);
}
#endif
