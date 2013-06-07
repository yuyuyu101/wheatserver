import sys
modules = sys.argv[1].split()
codes = '#include "wheatserver.h"\n'
for module in modules:
    codes += 'extern struct moduleAttr %s;\n' % module
codes += 'struct moduleAttr *ModuleTable[] = {\n'
for module in modules:
    codes += '&%s,\n' % module
codes += 'NULL};\n'
f = open("modules.c", 'r')
old_codes = f.read(10000)
f.close()
if old_codes != codes:
    f = open("modules.c", 'w')
    f.write(codes)
    f.close()
