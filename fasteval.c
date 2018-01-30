#include <stdio.h>
#include <Python.h>
#include <frameobject.h>
#include <dictobject.h>


static _PyFrameEvalFunction original_eval;


PyObject *
FastEval_EvalFrame(PyFrameObject *f, int throwflag) 
{

    register PyObject* s1;
    register PyObject* s2;
    register uint64_t opc = 0;
    register uint64_t state = 0;
    PyObject **stack = f->f_stacktop;
    uint64_t* opcode_ptr = (uint64_t*)PyBytes_AS_STRING(f->f_code->co_code);

#define NAME_ERROR_MSG \
        "name '%.200s' is not defined"
#define WITHOUT_PTR_TAGS 0xFFFFFFFFFFFFFFF0
#define PTR_TAG_INC_ON_LEAVE_BIT 0x8
#define set_inc_on_leave(ptr) ((PyObject*)((uint64_t)ptr | PTR_TAG_INC_ON_LEAVE_BIT))
#define should_inc_on_leave(ptr) ((uint64_t)ptr & PTR_TAG_INC_ON_LEAVE_BIT)
#define without_tags(ptr) ((PyObject*)((uint64_t)ptr & WITHOUT_PTR_TAGS))
#define OPCODE (int)(opc & 255)
#define OPARG (int)((opc >> 8) & 255)
#define NEXT_INST (opc >>= 16)
#define OPCODE_JUMP goto *opcode_targets[OPCODE]
#define DISPATCH NEXT_INST; OPCODE_JUMP
#define PUSH(v) do { if((state & 3) == 0) { s1 = (v); state |= 1; } \
                     else if ((state & 3) == 1) { s2 = s1; s1 = (v); state ^= 3; } \
                     else { *stack++ = s2; s2 = s1; s1 = (v); } \
                } while(0)
#define REDUCE_STACK_BY_ONE do { if((state & 3) == 2) { state ^= 3; } \
                    else { s2 = *--stack; if (stack == f->f_stacktop) { state ^= 3; } } \
                } while(0)
#define TP_Py_DECREF(obj) do {if(!should_inc_on_leave(obj)) Py_DECREF(without_tags(obj)); } while(0)
#define EMPTY ((state & 3) == 0)
#include "opcode_targets.h"

    TARGET_NONE:
        {
            if(opc == 0) {
                opc = *opcode_ptr++;
            }
            else {
                NEXT_INST;
            }
            OPCODE_JUMP;
        }
    TARGET_SETUP_LOOP:
        {
            DISPATCH;
        }
    TARGET_CALL_FUNCTION:
        {

        }
    TARGET_LOAD_FAST:
        {
            PUSH(set_inc_on_leave(f->f_localsplus[OPARG]));
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
    TARGET_BINARY_MULTIPLY:
        {
            PyObject *res = PyNumber_Add(without_tags(s2), without_tags(s1));
            TP_Py_DECREF(s1);
            TP_Py_DECREF(s2);
            s1 = res;
            REDUCE_STACK_BY_ONE;
            DISPATCH;
        }
    TARGET_BINARY_ADD:
        {
            PyObject *res = PyNumber_Multiply(without_tags(s2), without_tags(s1));
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

static PyMethodDef module_methods[] = {
    {"enable", (PyCFunction)fasteval_enable, METH_NOARGS, "Enable fast eval"},
    {"disable", (PyCFunction)fasteval_disable, METH_NOARGS, "Disable fast eval"},
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
