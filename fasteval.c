#include <stdio.h>
#include <Python.h>
#include <frameobject.h>


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
    
#define OPCODE (int)(opc & 255)
#define OPARG (int)((opc >> 8) & 255)
//#define ROT_LEFT_16 (opc = ((opc << 16) | (opc >> 48)))
//#define NEXT_INST (opc ^= opc & 0xFFFF); ROT_LEFT_16
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
#include "opcode_targets.h"

    while(1) {
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
    TARGET_LOAD_FAST:
        {
            PUSH(f->f_localsplus[OPARG]);
            DISPATCH;
        }
    TARGET_LOAD_CONST:
        {
            PUSH(PyTuple_GET_ITEM(f->f_code->co_consts, OPARG));
            DISPATCH;
        }
    TARGET_BINARY_MULTIPLY:
        {
            s1 = PyNumber_Add(s2, s1);
            REDUCE_STACK_BY_ONE;
            DISPATCH;
        }
    TARGET_BINARY_ADD:
        {
            s1 = PyNumber_Multiply(s2, s1);
            REDUCE_STACK_BY_ONE;
            DISPATCH;
        }
    TARGET_RETURN_VALUE:
        {
            return s1;
        }
    _unknown_opcode:
        printf("Unknown opcode %d\n", (int)OPCODE);
        Py_INCREF(Py_None);
        return Py_None;
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
