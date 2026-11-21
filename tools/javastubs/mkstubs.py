#!/usr/bin/env python3
"""Generate compile-only Java stubs for the head-unit classes a compiled jar references.

usage: mkstubs.py <dir with extracted .class files> <out src dir> <own package prefix, e.g. vwcn/>
Reads `javap -v -c -p` of every class, collects Methodref / InterfaceMethodref / Fieldref / Class entries whose
owner is not ours and not java.*, notes static access (invokestatic/getstatic/putstatic) and constructors,
and writes one public stub per referenced class with exactly those members (descriptors taken verbatim).
"""
import os, re, subprocess, sys

cls_dir, out_dir, own = sys.argv[1], sys.argv[2], sys.argv[3]
JAVAP = os.environ.get('JAVAP', 'javap')
files = []
for r, _, fs in os.walk(cls_dir):
    files += [os.path.join(r, f) for f in fs if f.endswith('.class')]

ext = lambda c: not c.startswith(own) and not c.startswith('java/') and not c.startswith('[')
classes = {}  # name -> {'iface':bool, 'methods':{(name,desc):static}, 'fields':{(name,desc):static}, 'super':..}

def C(n):
    return classes.setdefault(n, {'iface': False, 'methods': {}, 'fields': {}, 'super': None})

def types_in(desc):
    return re.findall(r'L([^;]+);', desc)

for f in files:
    out = subprocess.run([JAVAP, '-v', '-c', '-p', f], capture_output=True, text=True).stdout
    cp = {}
    for m in re.finditer(r'^\s*#(\d+) = (Methodref|InterfaceMethodref|Fieldref|Class)\s+\S+\s+// (.*)$', out, re.M):
        idx, kind, rest = int(m.group(1)), m.group(2), m.group(3).strip()
        if kind == 'Class':
            cp[idx] = ('Class', rest.strip('"'), None, None)
        else:
            mm = re.match(r'"?([^.]+)"?\.("?[^:]+"?):(.*)$', rest)
            owner, name, desc = mm.group(1).strip('"'), mm.group(2).strip('"'), mm.group(3)
            cp[idx] = (kind, owner, name, desc)
    # -target 1.4 compiles X.class to Class.forName("X") via a synthetic class$ -> String constants
    for m in re.finditer(r'^\s*#\d+ = String\s+\S+\s+// ((?:de|org|com)\.[\w.$]+)\s*$', out, re.M):
        C(m.group(1).replace('.', '/'))
    static_refs = set(int(x) for x in re.findall(r'(?:invokestatic|getstatic|putstatic)\s+#(\d+)', out))
    for idx, (kind, owner, name, desc) in cp.items():
        if kind == 'Class':
            for t in ([owner] if not owner.startswith('[') else types_in(owner)):
                if ext(t): C(t)
            continue
        for t in types_in(desc):
            if ext(t): C(t)
        if not ext(owner):
            continue
        c = C(owner)
        if kind == 'InterfaceMethodref':
            c['iface'] = True
        if kind == 'Fieldref':
            c['fields'][(name, desc)] = idx in static_refs
        else:
            c['methods'][(name, desc)] = idx in static_refs
    # our class header: super class / interfaces
    hm = re.search(r'^[^\n]*?(?:class|interface) (\S+)(?: extends ([\w.$]+))?(?: implements ([\w.$, ]+))?\s*$', out, re.M)
    if hm:
        sup = hm.group(2)
        if sup and ext(sup.replace('.', '/')):
            C(sup.replace('.', '/'))
        for i in (hm.group(3) or '').split(','):
            i = i.strip().replace('.', '/')
            if i and ext(i):
                C(i)['iface'] = True

# type hierarchy is not recorded in the caller's bytecode (upcasts need no checkcast): hints file, one line
# "sub/Class -> iface/or/Super" per relation
HINTS = {}
if len(sys.argv) > 4:
    for line in open(sys.argv[4]):
        line = line.split('#')[0].strip()
        if '->' in line:
            a, b = [x.strip() for x in line.split('->')]
            HINTS.setdefault(a, []).append(b)
            C(a); C(b)["iface"] = True   # hint targets are interfaces

def jtype(d, i=0):
    c = d[i]
    prim = {'B': 'byte', 'C': 'char', 'D': 'double', 'F': 'float', 'I': 'int', 'J': 'long', 'S': 'short', 'Z': 'boolean', 'V': 'void'}
    if c in prim: return prim[c], i + 1
    if c == 'L':
        e = d.index(';', i)
        return d[i + 1:e].replace('/', '.').replace('$', '.'), e + 1
    if c == '[':
        t, j = jtype(d, i + 1)
        return t + '[]', j
    raise ValueError(d)

def parse_m(desc):
    args, i = [], 1
    while desc[i] != ')':
        t, i = jtype(desc, i); args.append(t)
    ret, _ = jtype(desc, i + 1)
    return args, ret

def default(ret):
    if ret == 'void': return ''
    if ret == 'boolean': return ' return false;'
    if ret in ('byte', 'char', 'short', 'int', 'long', 'float', 'double'): return ' return 0;'
    return ' return null;'

# nested classes (Outer$Inner) are emitted inside their outer stub
top = {n: c for n, c in classes.items() if '$' not in n}
nested = {}
for n in classes:
    if '$' in n:
        outer = n.split('$')[0]
        nested.setdefault(outer, []).append(n)
        top.setdefault(outer, C(outer))

def body(n, c, indent, is_nested):
    simple = n.split('/')[-1].split('$')[-1]
    kw = 'interface' if c['iface'] else 'class'
    mods = 'public ' + ('static ' if is_nested else '')
    ext_cls, impl = '', []
    for h in HINTS.get(n, []):
        if classes[h]['iface']: impl.append(h.replace('/', '.').replace('$', '.'))
        else: ext_cls = ' extends ' + h.replace('/', '.').replace('$', '.')
    if impl: ext_cls += (' extends ' if c['iface'] else ' implements ') + ', '.join(impl)
    L = ['%s%s%s %s%s {' % (indent, mods, kw, simple, ext_cls)]
    for (name, desc), st in sorted(c['fields'].items()):
        t, _ = jtype(desc)
        L.append('%s  public %s%s %s;' % (indent, 'static ' if st else '', t, name))
    for (name, desc), st in sorted(c['methods'].items()):
        args, ret = parse_m(desc)
        params = ', '.join('%s a%d' % (t, k) for k, t in enumerate(args))
        if name == '<init>':
            L.append('%s  public %s(%s) {}' % (indent, simple, params))
        elif c['iface']:
            L.append('%s  %s %s(%s);' % (indent, ret, name, params))
        else:
            L.append('%s  public %s%s %s(%s) {%s }' % (indent, 'static ' if st else '', ret, name, params, default(ret)))
    if not c['iface'] and not any(k[0] == '<init>' and k[1] == '()V' for k in c['methods']):
        L.append('%s  public %s() {}' % (indent, simple))
    for inner in sorted(nested.get(n, [])):
        L += body(inner, classes[inner], indent + '  ', True)
    L.append(indent + '}')
    return L

for n, c in top.items():
    pkg = '.'.join(n.split('/')[:-1])
    path = os.path.join(out_dir, n + '.java')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as fh:
        fh.write('package %s;\n' % pkg + '\n'.join(body(n, c, '', False)) + '\n')
print('stubs:', len(top), 'top-level,', sum(len(v) for v in nested.values()), 'nested')
