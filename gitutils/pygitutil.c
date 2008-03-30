#include <Python.h>
#include "libgitread.h"

static PyObject *gu_pack_get_object(PyObject *self, PyObject *args)
{
    char *location;
    int full = 0;
    unsigned int offset;
    struct git_object g_obj;
    int ret;

    if(!PyArg_ParseTuple(args, "si|i", &location, &offset, &full))
        return NULL;

    ret = pack_get_object(location, offset, &g_obj, full);
    
    if(ret != 0) {
        PyErr_SetString(PyExc_Exception, "error occured while getting a packed object; perhaps the pack file doesn't exist or is corrupt.");
        return NULL;
    }
    
    if(g_obj.data != NULL) {
        return Py_BuildValue("iiS", g_obj.type, g_obj.size, PyFile_FromFile(g_obj.data, "temporary", "wr", NULL));
    } else {
        return Py_BuildValue("iiO", g_obj.type, g_obj.size, Py_None);
    }
}

static PyObject *gu_loose_get_object(PyObject *self, PyObject *args)
{
    char *location;
    int full = 0;
    struct git_object g_obj;
    int ret;

    if(!PyArg_ParseTuple(args, "s|i", &location, &full))
        return NULL;

    ret = loose_get_object(location, &g_obj, full);
    
    if(ret != 0) {
        PyErr_SetString(PyExc_Exception, "error occured while getting a loose object; perhaps the object's file doesn't exist or is corrupt.");
        return NULL;
    }
    
    if(g_obj.data != NULL) {
        return Py_BuildValue("iiS", g_obj.type, g_obj.size, PyFile_FromFile(g_obj.data, "temporary", "wr", NULL));
    } else {
        return Py_BuildValue("iiO", g_obj.type, g_obj.size, Py_None);
    }
}

static PyObject *gu_pack_idx_read(PyObject *self, PyObject *args)
{
    char *location;
    char *sha1;
    struct idx_entry *entry;
    PyObject *ret;
    
    if(!PyArg_ParseTuple(args, "ss", &location, &sha1))
        return NULL;

    entry = pack_idx_read(location, sha1);
        
    if(entry != NULL) {
        ret = Py_BuildValue("is", entry->offset, entry->sha1);
        free(entry);
        return ret;
    } else {
        return Py_BuildValue("iO", 0, Py_None);
    }
}

static PyMethodDef git_util_methods[] = {
    {"loose_get_object", gu_loose_get_object, METH_VARARGS, "Doc..."},
    {"pack_idx_read", gu_pack_idx_read, METH_VARARGS, "Doc..."},
    {"pack_get_object", gu_pack_get_object, METH_VARARGS, "Doc..."},
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initgitutil(void)
{
    (void) Py_InitModule("gitutil", git_util_methods);
}