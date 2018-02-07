#include <stdio.h>
#include <Python.h>
#include <frameobject.h>
#include <dictobject.h>


static _PyFrameEvalFunction original_eval;


static void
format_exc_check_arg(PyObject *exc, const char *format_str, PyObject *obj)
{
    const char *obj_str;

    if (!obj)
        return;

    obj_str = PyUnicode_AsUTF8(obj);
    if (!obj_str)
        return;

    PyErr_Format(exc, format_str, obj_str);
}


PyObject *
FastEval_EvalFrame(PyFrameObject *f, int throwflag) 
{
    register PyObject* s1;
    register PyObject* s2;
    register uint64_t state = 0;
    register PyObject **stack = f->f_stacktop;
    uint16_t* opcode_ptr = (uint16_t*)PyBytes_AS_STRING(f->f_code->co_code);

#define NAME_ERROR_MSG \
        "name '%.200s' is not defined"
#define WITHOUT_PTR_TAGS 0xFFFFFFFFFFFFFFF8
#define PTR_TAG_BLOCK 0x4
#define PTR_TAG_INC_ON_LEAVE_BIT 0x2
#define set_inc_on_leave(ptr) ((PyObject*)((uint64_t)ptr | PTR_TAG_INC_ON_LEAVE_BIT))
#define should_inc_on_leave(ptr) ((uint64_t)ptr & PTR_TAG_INC_ON_LEAVE_BIT)
#define without_tags(ptr) ((PyObject*)((uint64_t)ptr & WITHOUT_PTR_TAGS))
#define OPCODE (int)((*opcode_ptr) & 255)
#define OPARG (int)(((*opcode_ptr) >> 8) & 255)
#define NEXT_INST (opcode_ptr++)
//#define OPCODE_JUMP printf("%p %d %d\n", opcode_ptr, OPCODE, OPARG); goto *opcode_targets[OPCODE]
#define OPCODE_JUMP goto *opcode_targets[OPCODE]
#define DISPATCH NEXT_INST; OPCODE_JUMP
#define JUMPBY(x) (opcode_ptr += (x/2) + 1)
#define PUSH(v) do { if((state & 3) == 0) { s1 = (v); state |= 1; } \
                     else if ((state & 3) == 1) { s2 = s1; s1 = (v); state ^= 3; } \
                     else { *stack++ = s2; s2 = s1; s1 = (v); state |= 3; } \
                } while(0)
#define REDUCE_STACK_BY_ONE do { if((state & 3) == 2) { state ^= 3; } \
                    else { s2 = *--stack; if (stack == f->f_stacktop) { state ^= 1; } } \
                } while(0)
#define POP_TOP do { if((state & 3) == 1) { state ^= 1; } \
                     else if ((state & 3) == 2) { s1 = s2; state ^= 3; } \
                     else { s1 = s2; s2 = *--stack; if (stack == f->f_stacktop) { state ^= 1; }} \
                } while(0)
#define TP_Py_DECREF(obj) do {if(!should_inc_on_leave(obj)) Py_DECREF(without_tags(obj)); } while(0)
//#define TP_Py_DECREF(obj) do {if(!should_inc_on_leave(obj)) { Py_DECREF(without_tags(obj)); printf("DEREF YES\n"); } else { printf("DEREF NO\n"); } } while(0)
#define EMPTY ((state & 3) == 0)
#include "opcode_targets.h"
    
    OPCODE_JUMP;
    TARGET_SETUP_LOOP:
        {

            if(!EMPTY) {
                s1 = (PyObject*)((uint64_t)s1 | PTR_TAG_BLOCK);
            }
            DISPATCH;
        }
    TARGET_POP_BLOCK:
        {
            DISPATCH;
        }
    TARGET_JUMP_ABSOLUTE:
        {
            opcode_ptr = ((uint16_t*)PyBytes_AS_STRING(f->f_code->co_code)) + (OPARG / 2);
            OPCODE_JUMP;
        }
    TARGET_LOAD_FAST:
        {
            PUSH(set_inc_on_leave(f->f_localsplus[OPARG]));
            DISPATCH;
        }
    TARGET_STORE_FAST:
        {
            f->f_localsplus[OPARG] = s1;
            POP_TOP;
            DISPATCH;
        }
    TARGET_LOAD_CONST:
        {
            PUSH(set_inc_on_leave(PyTuple_GET_ITEM(f->f_code->co_consts, OPARG)));
            DISPATCH;
        }
    TARGET_LOAD_GLOBAL:
        {
			PyObject *name = PyTuple_GET_ITEM(f->f_code->co_names, OPARG);
			PyObject *res  = _PyDict_LoadGlobal((PyDictObject *)f->f_globals,
                                       (PyDictObject *)f->f_builtins,
                                       name);
			if (res == NULL) {
				if (!_PyErr_OCCURRED()) {
					/* _PyDict_LoadGlobal() returns NULL without raising
					 * an exception if the key doesn't exist */
					format_exc_check_arg(PyExc_NameError,
										 NAME_ERROR_MSG, name);
				}
				goto error;
			}
			PUSH(set_inc_on_leave(res));
            DISPATCH;
        }
    TARGET_GET_ITER:
        {
            PyObject *iter = PyObject_GetIter(without_tags(s1));
            TP_Py_DECREF(s1);
			s1 = iter;
            if (iter == NULL)
                goto error;
            DISPATCH;
        }
	TARGET_FOR_ITER:
		{
            PyObject *iter = without_tags(s1);
            PyObject *next = (*iter->ob_type->tp_iternext)(iter);
            if (next != NULL) {
                PUSH(next);
                DISPATCH;
            }
            if (PyErr_Occurred()) {
                if (!PyErr_ExceptionMatches(PyExc_StopIteration))
                    goto error;
                PyErr_Clear();
            }
            /* iterator ended normally */
            TP_Py_DECREF(s1);
            POP_TOP;
            JUMPBY(OPARG);
            OPCODE_JUMP;
		}
    TARGET_BINARY_MULTIPLY:
        {
            PyObject *res = PyNumber_Multiply(without_tags(s2), without_tags(s1));
            TP_Py_DECREF(s1);
            TP_Py_DECREF(s2);
            s1 = res;
            REDUCE_STACK_BY_ONE;
            DISPATCH;
        }
    TARGET_BINARY_ADD:
        {
            PyObject *res = PyNumber_Add(without_tags(s2), without_tags(s1));
            TP_Py_DECREF(s1);
            TP_Py_DECREF(s2);
            s1 = res;
            REDUCE_STACK_BY_ONE;
            DISPATCH;
        }
    TARGET_RETURN_VALUE:
        {
            if(should_inc_on_leave(s1)) {
                PyObject *res = without_tags(s1);
                Py_INCREF(res);
                return res;
            }
            else {
                return without_tags(s1);
            }
        }
    _unknown_opcode:
        printf("Unknown opcode %d\n", (int)OPCODE);
    fallback:
        printf("Fallback!!!\n");
        return original_eval(f, throwflag);
	error:
		PyTraceBack_Here(f);
		return NULL;
}


static PyObject*
fasteval_decode1(PyObject *self, PyObject *args) {
    char *bytecodes;
    if(!PyArg_ParseTuple(args, "s", &bytecodes)) {
        return NULL;
    }
    

}


static PyObject *
fasteval_enable(PyObject *self, PyObject *args)
{
    PyThreadState *tstate = PyThreadState_GET();
    original_eval = tstate->interp->eval_frame;
    tstate->interp->eval_frame = FastEval_EvalFrame;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
fasteval_disable(PyObject *self, PyObject *args)
{
    PyThreadState *tstate = PyThreadState_GET();
    tstate->interp->eval_frame = original_eval;
    Py_INCREF(Py_None);
    return Py_None;
}

#define TEST_LOOP_SIZE 1000000

static PyObject *
fasteval_test_pyint_add(PyObject *self, PyObject *args)
{
    int i;
    PyObject *res = PyLong_FromLong(1);
    PyObject *v1 = PyLong_FromLong(1);
    for(i = 0; i < TEST_LOOP_SIZE; i++) {
        res = PyNumber_Add(res, v1);
    }
    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject *
fasteval_test_tagged_int_add(PyObject *self, PyObject *args)
{
    int i;
    int64_t res = 3;
    int64_t v1 = 3;
    for(i = 0; i < TEST_LOOP_SIZE; i++) {
        if (res & 1 && v1 & 1) {
            res = (((res >> 1) + (v1 >> 1)) << 1) | 1;
        }
        else {
            res = (void*)PyNumber_Add((PyObject*)res, (PyObject*)v1);
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
} 

static PyObject *
fasteval_get_frame_stack_item(PyObject *self, PyObject *args)
{
    int index;
    PyFrameObject *f;
    if(!PyArg_ParseTuple(args, "O!i", &PyFrame_Type, &f, &index)) {
        return NULL;
    }
    if(f->f_stacktop == NULL) {
        PyErr_SetString(PyExc_ValueError, "Frame f_stacktop is NULL. Only expected to work in trace functions.");
        return NULL;
    }
    int stack_depth = f->f_stacktop - f->f_valuestack;
    if(index >= stack_depth) {
        PyErr_SetObject(PyExc_ValueError, PyUnicode_FromFormat("Out of range. Index=%d, stack-depth=%d", index, stack_depth));
        return NULL;
    }
    PyObject *obj = f->f_stacktop[-(index+1)];
    Py_INCREF(obj);
    return obj;
} 

static PyObject *
fasteval_get_frame_stack_depth(PyObject *self, PyObject *args)
{
    PyFrameObject *f;
    if(!PyArg_ParseTuple(args, "O!", &PyFrame_Type, &f)) {
        return NULL;
    }
    if(f->f_stacktop == NULL) {
        PyErr_SetString(PyExc_ValueError, "Frame f_stacktop is NULL. Only expected to work in trace functions.");
        return NULL;
    }
    long stack_depth = f->f_stacktop - f->f_valuestack;
    return PyLong_FromLong(stack_depth);
} 

static PyMethodDef module_methods[] = {
    {"enable", (PyCFunction)fasteval_enable, METH_NOARGS, "Enable fast eval"},
    {"disable", (PyCFunction)fasteval_disable, METH_NOARGS, "Disable fast eval"},
    {"test_pyint_add", (PyCFunction)fasteval_test_pyint_add, METH_NOARGS, ""},
    {"test_tagged_int_add", (PyCFunction)fasteval_test_tagged_int_add, METH_NOARGS, ""},
    {"get_frame_stack_item", (PyCFunction)fasteval_get_frame_stack_item, METH_VARARGS, ""},
    {"get_frame_stack_depth", (PyCFunction)fasteval_get_frame_stack_depth, METH_VARARGS, ""},
    {NULL, NULL, 0, NULL}
};


static struct PyModuleDef fasteval_module = {
   PyModuleDef_HEAD_INIT,
   "fasteval",
   NULL,
   -1,
   module_methods
};

PyMODINIT_FUNC
PyInit_fasteval(void)
{
    return PyModule_Create(&fasteval_module);
}
