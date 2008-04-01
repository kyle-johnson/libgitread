#include <Python.h>
#include "libgitread.h"

static PyObject *raw_tree_to_pyobject(struct git_object *g_obj)
{
    unsigned char *source_internal_buffer, *src_buff, *end;
    PyObject *pylist;
    PyObject *pytuple;
    PyObject *pyfilename;
    PyObject *pysha1;
    PyObject *pyint;
    
    if(!(source_internal_buffer = (unsigned char *) malloc(g_obj->size)))
        return NULL;
    
    if(!(pylist = PyList_New(0))) {
        free(source_internal_buffer);
        return NULL;
    }
    
    fseek(g_obj->data, 0, SEEK_SET);
    fread(source_internal_buffer, 1, g_obj->size, g_obj->data);
    src_buff = source_internal_buffer;
    
    end = (unsigned char *) src_buff + g_obj->size;
    
    while(src_buff < end) {
        // each list entry is a tuple with three parts: (mode, sha1, name)
        if(!(pytuple = PyTuple_New(3))) {
            free(source_internal_buffer);
            return NULL;
        }
        
        pyint = PyInt_FromLong((long) atoi((char *) src_buff));
        src_buff = memchr(src_buff, ' ', 10); // find the space between the mode and the filename
        src_buff++; // move past the space
        pyfilename = PyString_FromString((char *) src_buff);
        src_buff = memchr(src_buff, '\0', 1024); // get the \0 after the filename
        src_buff++; // move past the \0
        pysha1 = PyString_FromString(sha1_to_hex(src_buff));
        src_buff += 20; // move past the binary sha1

        if(pyint == NULL)
            Py_INCREF(Py_None);
        if(PyTuple_SetItem(pytuple, 0, (pyint != NULL) ? pyint : Py_None) != 0) {
            // something REALLY bad is happing...
            printf("ZOMG!!!! REALLY BAD!!!! 1\n\n\n");
            return NULL;
        }
        if(pysha1 == NULL)
            Py_INCREF(Py_None);
        if(PyTuple_SetItem(pytuple, 1, (pysha1 != NULL) ? pysha1 : Py_None) != 0) {
            // something REALLY bad is happing...
            printf("ZOMG!!!! REALLY BAD!!!! 2 \n\n\n");
            return NULL;
        }
        if(pyfilename == NULL)
            Py_INCREF(Py_None);
        if(PyTuple_SetItem(pytuple, 2, (pyfilename != NULL) ? pyfilename : Py_None) != 0) {
            // something REALLY bad is happing...
            printf("ZOMG!!!! REALLY BAD!!!! 3\n\n\n");
            return NULL;
        }
        
        if(PyList_Append(pylist, pytuple) != 0) {
            printf("Umm, can't insert the tuple into the list???");
            return NULL;
        }
    }
    
    free(source_internal_buffer);
    
    return pylist;
}

static PyObject *gu_pack_get_object(PyObject *self, PyObject *args)
{
    char *location;
    int full = 0;
    unsigned int offset;
    struct git_object g_obj;
    int ret;
    PyObject *pytree;

    if(!PyArg_ParseTuple(args, "si|i", &location, &offset, &full))
        return NULL;

    ret = pack_get_object(location, offset, &g_obj, full);
    
    if(ret != 0) {
        PyErr_SetString(PyExc_Exception, "error occured while getting a packed object; perhaps the pack file doesn't exist or is corrupt.");
        return NULL;
    }
    
    if(g_obj.data != NULL) {
        if(g_obj.type == TREE) {
            pytree = raw_tree_to_pyobject(&g_obj);
            fclose(g_obj.data); // we have to do this ourselves, just in case the previous call errored out
            return Py_BuildValue("iiO", g_obj.type, g_obj.size, pytree);
        }
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
    PyObject *pytree;

    if(!PyArg_ParseTuple(args, "s|i", &location, &full))
        return NULL;

    ret = loose_get_object(location, &g_obj, full);
    
    if(ret != 0) {
        PyErr_SetString(PyExc_Exception, "error occured while getting a loose object; perhaps the object's file doesn't exist or is corrupt.");
        return NULL;
    }
    
    if(g_obj.data != NULL) {
        if(g_obj.type == TREE) {
            pytree = raw_tree_to_pyobject(&g_obj);
            fclose(g_obj.data); // we have to do this ourselves, just in case the previous call errored out
            return Py_BuildValue("iiO", g_obj.type, g_obj.size, pytree);
        }
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