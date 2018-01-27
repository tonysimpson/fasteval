table = ['&&_unknown_opcode'] * 256
table[0] = '&&TARGET_NONE'
code = open('fasteval.c').read()
for line in open('../cpython/Include/opcode.h'):
    if line.startswith('#define '):
        try:
            _def, OPCODE_NAME, OPCODE_VALUE = line.split()
            if 'TARGET_'+OPCODE_NAME in code:
                table[int(OPCODE_VALUE)] = '&&TARGET_' + OPCODE_NAME
        except ValueError:
            pass
print('static void *opcode_targets[256] = {')
print('    ' + ',\n    '.join(table))
print('};')
print()
