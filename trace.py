import sys
import opcode
import fasteval

def test(a):
    return a * 7 + 2



def tracefunc(frame, event, arg):
    if event == 'call':
        frame.f_trace_opcodes = True
        frame.f_trace_lines = False
        return tracefunc
    if event == 'opcode':
        _opcode, _oparg = frame.f_code.co_code[frame.f_lasti], frame.f_code.co_code[frame.f_lasti+1]
        for i in range(fasteval.get_frame_stack_depth(frame)):
            o = fasteval.get_frame_stack_item(frame, i)
            print('Stack', i, '\t:\t', type(o), repr(o))
        print(opcode.opname[_opcode])

sys.settrace(tracefunc)

test(55)
