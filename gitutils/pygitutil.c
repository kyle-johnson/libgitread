#include <Python.h>
#include <structmember.h> // also a Python include

#include "libgitread.h"

typedef struct {
    PyObject_HEAD
    struct idx *idx;
    PyObject *location;
} PackIdxObject;

static void PackIdx_dealloc(PackIdxObject *self)
{
    // close and free up everything as needed...
    if(self->idx)
        unload_idx(self->idx);
    Py_XDECREF(self->location);
        
    // have to do this
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *PackIdx_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PackIdxObject *self;
    
    self = (PackIdxObject *)type->tp_alloc(type, 0);
    if(self != NULL) {
        // setup values
        self->idx = NULL;
        self->location = NULL;
    }
    
    return (PyObject *)self;
}

static int PackIdx_init(PackIdxObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *location;
    
    if(!PyArg_ParseTuple(args, "S", &location))
        return -1; // error
    
    // quick error check, since it is possible to call __init__ more than once (which we don't support)
    if(self->idx != NULL || self->location != NULL) {
        PyErr_SetString(PyExc_Exception, "This object has already been intialized once.");
        return -1;
    }
    
    // load up the idx
    self->idx = load_idx(PyString_AsString(location));
    
    if(!self->idx) {
        PyErr_SetString(PyExc_Exception, "Failed to load the idx file.");
        return -1;
    }
    
    // save the location for easy access from Python
    self->location = location;
    Py_INCREF(location);
    
    return 0; // success
}

/*static void PackIdx_close(PackIdxObject *self)
{
    
}*/

static PyMemberDef PackIdx_members[] = {
    {"location", T_OBJECT, offsetof(PackIdxObject, location), READONLY, "location of the idx file"},
    {NULL}
};

/*static PyMethodDef PackIdx_methods[] = {
    {"close", (PyCFunction)PackIdx_close, METH_NOARGS,
        "Closes the idx file properly. This method is called if you simply delete the object."},
    {NULL}
};*/

static PyTypeObject PackIdxType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "gitutil.PackIdx",         /*tp_name*/
    sizeof(PackIdxObject),     /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)PackIdx_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "PackIdx objects",         /* tp_doc */
    0,		                   /* tp_traverse */
    0,		                   /* tp_clear */
    0,		                   /* tp_richcompare */
    0,		                   /* tp_weaklistoffset */
    0,		                   /* tp_iter */
    0,		                   /* tp_iternext */
    0,//PackIdx_methods,           /* tp_methods */
    PackIdx_members,           /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)PackIdx_init,    /* tp_init */
    0,                         /* tp_alloc */
    PackIdx_new,               /* tp_new */
};

static PyObject *raw_tree_to_pyobject(struct git_object *g_obj)
{
    unsigned char /**source_internal_buffer,*/ *src_buff, *end;
    PyObject *pylist;
    PyObject *pytuple;
    PyObject *pyfilename;
    PyObject *pysha1;
    PyObject *pyint;
    
    if(!(pylist = PyList_New(0)))
        return NULL;
    
    //fseek(g_obj->data, 0, SEEK_SET);
    //fread(source_internal_buffer, 1, g_obj->size, g_obj->data);
    src_buff = g_obj->mem_data;
    
    end = (unsigned char *) src_buff + g_obj->size;
    
    while(src_buff < end) {
        // each list entry is a tuple with three parts: (mode, sha1, name)
        if(!(pytuple = PyTuple_New(3)))
            return NULL;
        
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
        //if(pyint != NULL)
        //    Py_DECREF(pyint);
        
        if(pysha1 == NULL)
            Py_INCREF(Py_None);
        if(PyTuple_SetItem(pytuple, 1, (pysha1 != NULL) ? pysha1 : Py_None) != 0) {
            // something REALLY bad is happing...
            printf("ZOMG!!!! REALLY BAD!!!! 2 \n\n\n");
            return NULL;
        }
        //if(pysha1 != NULL)
        //    Py_DECREF(pysha1);
        
        if(pyfilename == NULL)
            Py_INCREF(Py_None);
        if(PyTuple_SetItem(pytuple, 2, (pyfilename != NULL) ? pyfilename : Py_None) != 0) {
            // something REALLY bad is happing...
            printf("ZOMG!!!! REALLY BAD!!!! 3\n\n\n");
            return NULL;
        }
        //if(pyfilename != NULL)
        //    Py_DECREF(pyfilename);
        
        if(PyList_Append(pylist, pytuple) != 0) {
            printf("Umm, can't insert the tuple into the list???");
            return NULL;
        }
        Py_DECREF(pytuple);
    }
    
    //free(source_internal_buffer);
    
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
    PyObject *retObj;
    PyObject *buffstr;

    if(!PyArg_ParseTuple(args, "si|i", &location, &offset, &full))
        return NULL;

    ret = pack_get_object(location, offset, &g_obj, full);
    
    if(ret != 0) {
        printf("!!!! %i", ret);
        PyErr_SetString(PyExc_Exception, "error occured while getting a packed object; perhaps the pack file doesn't exist or is corrupt.");
        return NULL;
    }
    
    if(g_obj.mem_data != NULL) {
        if(g_obj.type == TREE) {
            pytree = raw_tree_to_pyobject(&g_obj);
            free(g_obj.mem_data);//fclose(g_obj.data); // we have to do this ourselves, just in case the previous call errored out
            retObj = Py_BuildValue("iiO", g_obj.type, g_obj.size, pytree);
            Py_DECREF(pytree);
            return retObj;
        }
        buffstr =  PyString_FromStringAndSize((char *) g_obj.mem_data, g_obj.size);
        retObj = Py_BuildValue("iiO", g_obj.type, g_obj.size, buffstr);
        Py_DECREF(buffstr); // this function is done messing with it, so it needs to give up its reference for proper gc
        free(g_obj.mem_data);
        return retObj;
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
    PyObject *retObj;
    PyObject *buffstr;

    if(!PyArg_ParseTuple(args, "s|i", &location, &full))
        return NULL;

    ret = loose_get_object(location, &g_obj, full);
    
    if(ret != 0) {
        PyErr_SetString(PyExc_Exception, "error occured while getting a loose object; perhaps the object's file doesn't exist or is corrupt.");
        return NULL;
    }
    
    if(g_obj.mem_data != NULL) {
        if(g_obj.type == TREE) {
            pytree = raw_tree_to_pyobject(&g_obj);
            free(g_obj.mem_data);//fclose(g_obj.data); // we have to do this ourselves, just in case the previous call errored out
            retObj = Py_BuildValue("iiO", g_obj.type, g_obj.size, pytree);
            Py_DECREF(pytree);
            return retObj;
        }
        buffstr = PyString_FromStringAndSize((char *) g_obj.mem_data, g_obj.size);
        retObj = Py_BuildValue("iiO", g_obj.type, g_obj.size, buffstr);
        Py_DECREF(buffstr);
        free(g_obj.mem_data);
        return retObj;
    } else {
        return Py_BuildValue("iiO", g_obj.type, g_obj.size, Py_None);
    }
}

static PyObject *gu_pack_idx_read(PyObject *self, PyObject *args)
{
    PackIdxObject *idx;
    char *sha1;
    struct idx_entry *entry;
    struct sha1 hash;
    PyObject *ret;
    
    if(!PyArg_ParseTuple(args, "Os", &idx, &sha1))
        return NULL;

    if(str_sha1_to_sha1_obj(sha1, &hash) != 0)
        return NULL;
    
    entry = pack_idx_read(idx->idx, &hash);
    
    if(entry != NULL) {
        ret = Py_BuildValue("is", entry->offset, sha1_to_hex(entry->sha1));
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
    PyObject *m;
    
    // initial type setup
    if(PyType_Ready(&PackIdxType) < 0)
        return;
    
    m = Py_InitModule("gitutil", git_util_methods);
    
    if(m == NULL)
        return;
    
    // now actually add the type
    Py_INCREF(&PackIdxType);
    PyModule_AddObject(m, "PackIdx", (PyObject *)&PackIdxType);
}