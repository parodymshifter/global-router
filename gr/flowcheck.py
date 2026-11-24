#!/usr/bin/env python3
"""flowcheck.py -- checks a flow script runs before it starts any work (HISTORY_COST_SWEEP.md Part 216, 2026-11-24).

The user's rule (2026-11-24): every option, setting and input is checked before any work, and every problem is reported,
in detail, before the run stops. pd39route does this itself (its RouterRun); the flow scripts that drive it
(timing_loop.py, reorder_portfolio.py, elmore_rows.py) collect their problems here and stop with all of them.

  p = Problems('timing_loop.py')
  p.need_file(path, 'the design .cap')       # exists, a file, readable
  p.need_dir(path, 'the --reuse folder')     # exists, a folder
  p.need_exe(path, 'OpenROAD')               # exists, executable
  p.need_output(path, '--out')               # its folder exists and can be written; not a folder itself
  p.need_workdir(path, 'WORK_DIR')           # an existing writable folder, or one that can be made
  p.add('free text')
  p.stop_if_any(usage)                       # prints them all, numbered, and exits 2
Standard library only.
"""
import os
import sys


class Problems:
    def __init__(self, prog):
        self.prog = prog
        self.items = []

    def add(self, msg):
        if msg not in self.items:
            self.items.append(msg)

    def need_file(self, path, what):
        if not path:
            self.add(f'{what}: no file named')
        elif not os.path.exists(path):
            self.add(f'{what} {path} does not exist')
        elif os.path.isdir(path):
            self.add(f'{what} {path} is a folder, not a file')
        elif not os.access(path, os.R_OK):
            self.add(f'{what} {path} cannot be read')
        else:
            return True
        return False

    def need_dir(self, path, what):
        if not path or not os.path.isdir(path):
            self.add(f'{what} {path} is not a folder' if path and os.path.exists(path) else f'{what} {path} does not exist')
            return False
        return True

    def need_exe(self, path, what):
        if not path or not os.path.isfile(path):
            self.add(f'{what} {path} does not exist')
        elif not os.access(path, os.X_OK):
            self.add(f'{what} {path} is not executable')
        else:
            return True
        return False

    def need_output(self, path, what):
        if not path:
            self.add(f'{what}: no file named')
            return False
        if os.path.isdir(path):
            self.add(f'{what} {path} is a folder')
            return False
        if os.path.exists(path):
            if not os.access(path, os.W_OK):
                self.add(f'{what} {path} exists and cannot be written')
                return False
            return True
        d = os.path.dirname(os.path.abspath(path))
        if not os.path.isdir(d):
            self.add(f'{what} {path}: its folder {d} does not exist')
        elif not os.access(d, os.W_OK | os.X_OK):
            self.add(f'{what} {path}: its folder {d} cannot be written')
        else:
            return True
        return False

    def need_workdir(self, path, what):
        a = os.path.abspath(path)
        if os.path.exists(a):
            if not os.path.isdir(a):
                self.add(f'{what} {path} is not a folder')
            elif not os.access(a, os.W_OK | os.X_OK):
                self.add(f'{what} {path} cannot be written')
            else:
                return True
            return False
        d = a
        while not os.path.exists(d):  # the nearest existing ancestor must let us make the rest
            d = os.path.dirname(d)
        if not os.path.isdir(d) or not os.access(d, os.W_OK | os.X_OK):
            self.add(f'{what} {path} cannot be made ({d} cannot be written)')
            return False
        return True

    def stop_if_any(self, usage=None):
        if not self.items:
            return
        n = len(self.items)
        print(f'{self.prog}: {n} problem{"" if n == 1 else "s"} found before any work; nothing was run:', file=sys.stderr)
        for i, m in enumerate(self.items, 1):
            lines = str(m).split('\n')
            print(f'  {i}. {lines[0]}', file=sys.stderr)
            for l in lines[1:]:
                print(' ' * (4 + len(str(i))) + l, file=sys.stderr)
        if usage:
            print(usage.rstrip('\n'), file=sys.stderr)
        sys.exit(2)


def parse_options(argv, spec, positional, prog):
    """argv (without the program name) against spec {option: None (a switch) | 'value'}: returns (opts, args, problems)
    with every unknown option, missing value, repeat and wrong positional count listed (the values are checked by the
    caller). An argument that starts with '-' and is not in spec is unknown -- never a positional."""
    p = Problems(prog)
    opts, args, i = {}, [], 0
    while i < len(argv):
        a = argv[i]
        if a.startswith('--') or (a.startswith('-') and len(a) > 1 and not a[1:2].isdigit()):
            if a not in spec:
                near = [k for k in spec if _close(a, k)]
                p.add(f'unknown option {a}' + (f' (did you mean {near[0]}?)' if near else ''))
            elif a in opts:
                p.add(f'{a} is given twice')
            elif spec[a] is None:
                opts[a] = True
            elif i + 1 >= len(argv) or (argv[i + 1].startswith('--')):
                p.add(f'{a} needs a value')
            else:
                opts[a] = argv[i + 1]
                i += 1
        else:
            args.append(a)
        i += 1
    if positional is not None and len(args) != positional:
        p.add(f'{len(args)} argument{"" if len(args) == 1 else "s"} given ({", ".join(repr(x) for x in args) or "none"}); '
              f'{positional} expected (the value after an unknown option counts as one)')
    return opts, args, p


def _close(a, b):
    """True when a is one edit (insert, delete, replace, swap) from b."""
    if a == b:
        return False
    if abs(len(a) - len(b)) > 1:
        return False
    if len(a) == len(b):
        d = [i for i in range(len(a)) if a[i] != b[i]]
        return len(d) == 1 or (len(d) == 2 and d[1] == d[0] + 1 and a[d[0]] == b[d[1]] and a[d[1]] == b[d[0]])
    s, t = (a, b) if len(a) < len(b) else (b, a)
    return any(s == t[:i] + t[i + 1:] for i in range(len(t)))


def selftest():
    import tempfile
    t = tempfile.mkdtemp()
    f = os.path.join(t, 'f.txt')
    open(f, 'w').write('x')
    ok = True
    p = Problems('x')
    ok &= p.need_file(f, 'a file') and not p.need_file(os.path.join(t, 'no'), 'b') and not p.need_file(t, 'c')
    ok &= p.need_output(os.path.join(t, 'new.out'), 'd') and not p.need_output(os.path.join(t, 'no', 'x'), 'e')
    ok &= not p.need_output(t, 'f') and p.need_workdir(os.path.join(t, 'a', 'b'), 'g') and not p.need_workdir(f, 'h')
    ok &= len(p.items) == 5 and 'b ' + os.path.join(t, 'no') + ' does not exist' in p.items
    opts, args, q = parse_options(['d', '--out', 'o', '--lean', 'w'], {'--out': 'value', '--lean': None}, 2, 'x')
    ok &= opts == {'--out': 'o', '--lean': True} and args == ['d', 'w'] and not q.items
    opts, args, q = parse_options(['d', '--ot', 'o', '--lean', '--lean', '--out'], {'--out': 'value', '--lean': None}, 2, 'x')
    # the value of an unknown option is taken as a positional ('o' here), so the count still adds up
    ok &= q.items == ['unknown option --ot (did you mean --out?)', '--lean is given twice', '--out needs a value'] and \
        args == ['d', 'o']
    ok &= _close('--patiense', '--patience') and not _close('--refine', '--resume')
    print('flowcheck selftest: ' + ('pass' if ok else 'FAIL'))
    return ok


if __name__ == '__main__':
    sys.exit(0 if selftest() else 1)
