#!/usr/bin/env python3
"""Bound every complete S3 SDR record from its actual format and arguments.

Uses full 32/64-bit integer spellings and a conservative full finite-double
fixed-decimal bound. String bounds come from actual literal branches, source
identity widths, the temperature buffer, and static error call sites. The
SDK version has an explicit conservative 64-byte pinned-build assumption.
No RF, USB, target build or firmware modification is performed.
"""
import ast
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'main/tx_metal_s3.c'
text = SOURCE.read_text(encoding='utf-8')
capacity = int(re.search(r'^#define OUTPUT_CAPACITY (\d+)\s*$', text, re.M)[1])
assert capacity == 4096
assert 'static char output_buffer[OUTPUT_CAPACITY]' in text
assert '__attribute__((format(printf,1,2)))' in text

LITERAL = r'"(?:\\.|[^"\\])*"'
CONVERSION = re.compile(r'%([-+ #0]*)(\d*)(?:\.(\d+))?(hh|h|ll|l|j|z|t)?([diuoxXfFgGsce%])')


def arguments(start):
    """Split C call arguments, respecting nested calls, casts and strings."""
    depth, quote, escape, previous = 1, None, False, start
    args = []
    for pos in range(start, len(text)):
        ch = text[pos]
        if quote:
            if escape: escape = False
            elif ch == '\\': escape = True
            elif ch == quote: quote = None
            continue
        if ch in ('"', "'"): quote = ch
        elif ch == '(': depth += 1
        elif ch == ')':
            depth -= 1
            if not depth:
                args.append(text[previous:pos].strip())
                return args
        elif ch == ',' and depth == 1:
            args.append(text[previous:pos].strip()); previous = pos + 1
    raise AssertionError('Unterminated output call')


def literal_expression(expression):
    values = re.findall(LITERAL, expression)
    rest = re.sub(LITERAL, '', expression).strip()
    if rest: return None
    return ''.join(ast.literal_eval(v) for v in values)


errors = [ast.literal_eval(m[1]) for m in re.finditer(r'\berror\(\s*(' + LITERAL + r')\s*\)', text)]
assert errors
for call in re.finditer(r'\berror\s*\(', text):
    actual = arguments(call.end())
    assert len(actual) == 1
    assert literal_expression(actual[0]) is not None or actual[0] == 'const char *s', actual
backend_body = re.search(r'static const char \*backend_name\([^)]*\)\s*\{([^}]+)\}', text)[1]
backend_width = max(len(ast.literal_eval(v)) for v in re.findall(LITERAL, backend_body))
assert re.search(r'char number\[32\]', text)
assert 'char late_count[16],late_maximum[16]' in text
assert 'snprintf(late_count,sizeof(late_count),"%u",(unsigned)r.late_writes)' in text
assert 'snprintf(late_maximum,sizeof(late_maximum),"%u",(unsigned)r.late_max)' in text
string_assumptions = {'SDR_SOURCE_SHA': 40, 'SDR_SOURCE_DIGEST': 64, 'SDR_WORDS_DIGEST': 64, 'SDR_FAST_DIGEST': 64,
                      'esp_get_idf_version()': 64, 'number': 31, 's': max(map(len, errors)),
                      'backend_name(backend)': backend_width,
                      'backend==Fast?"null":late_count': 10,
                      'backend==Fast?"null":late_maximum': 10}


def string_width(expression):
    if expression in string_assumptions:
        return string_assumptions[expression]
    choices = re.findall(LITERAL, expression)
    # Remaining %s arguments are finite conditional selections of literals.
    if choices and '?' in expression:
        return max(len(ast.literal_eval(v)) for v in choices)
    literal = literal_expression(expression)
    if literal is not None: return len(literal)
    raise AssertionError(('Unbounded string argument', expression))


def format_bound(fmt, args):
    size, end, index = 1, 0, 0  # Include NUL, as the target formatter must.
    strings = []
    for match in CONVERSION.finditer(fmt):
        gap = fmt[end:match.start()]
        assert '%' not in gap, ('Unsupported conversion', gap)
        size += len(gap)
        flags, width, precision, length, kind = match.groups()
        width = int(width or 0); precision = int(precision) if precision is not None else None
        if kind == '%': size += 1; end = match.end(); continue
        assert index < len(args), ('Missing argument', fmt)
        expression = args[index]; index += 1
        if kind == 's':
            count = string_width(expression)
            if precision is not None: count = min(count, precision)
            strings.append({'expression': expression, 'max_bytes': count})
        elif kind in 'di': count = 20 if length in ('ll', 'j') else 11
        elif kind == 'u': count = 20 if length in ('ll', 'j') else 10
        elif kind in 'xX': count = 16 if length in ('ll', 'j') else 8
        elif kind == 'o': count = 22 if length in ('ll', 'j') else 11
        elif kind in 'fF': count = 1 + 309 + (1 + (precision if precision is not None else 6))
        elif kind in 'gGe': count = 32 + (precision or 6)
        elif kind == 'c': count = 1
        else: raise AssertionError(kind)
        size += max(width, count); end = match.end()
    assert '%' not in fmt[end:]
    size += len(fmt[end:])
    assert index == len(args), ('Argument count mismatch', index, len(args), fmt)
    return size, strings


records = []
for call in re.finditer(r'\boutput_(printf|puts)\s*\(', text):
    args = arguments(call.end())
    fmt = literal_expression(args[0])
    if fmt is None: continue  # Function definition; no first literal argument.
    if call[1] == 'puts':
        assert len(args) == 1; fmt += '\n'; values = []
    else: values = args[1:]
    if not fmt.startswith('SDR '):
        assert fmt == '%s\n' and values == ['text']  # Sole forwarding wrapper.
        continue
    assert fmt.endswith('\n') and fmt.count('\n') == 1
    kind = re.search(r'"type":"([^"%]+)"', fmt)
    assert kind, fmt
    bound, strings = format_bound(fmt, values)
    assert bound <= capacity, (kind[1], bound, capacity)
    records.append({'type': kind[1], 'upper_bound_including_NUL': bound,
                    'capacity': capacity, 'string_arguments': strings})

expected = {'temperature', 'error', 'info', 'radio_error', 'tx_result', 'tx_trace', 'tx_segment',
            'tx_end', 'tx_fast_clock', 'tx_source_integrity', 'reserved', 'observe', 'init_path', 'permit', 'ready', 'loaded', 'tx_starting'}
assert {r['type'] for r in records} == expected
assert len(records) == len(expected)
for name in ('raw_store_loop', 'timed_loop', 'fast_store_loop'):
    return_type = 'FastResult' if name == 'fast_store_loop' else 'Result'
    prefix = 'static ' + return_type + ' IRAM_ATTR __attribute__((noinline)) ' + name
    definition = re.search(re.escape(prefix) + r'\([^;{}]*\)\s*\{', text)
    assert definition, ('Missing actual loop definition', name)
    begin = definition.start()
    body = text[begin:text.index('\n}', begin)+2]
    assert not re.search(r'\boutput_(?:printf|puts|flush|write_all)\s*\(', body)
assert not re.search(r'(?<![\w])(?:printf|puts)\s*\(', text)

print(json.dumps({'schema': 'sdr-metal-tx-s3-output-bounds/1', 'evidence_class': 'STATIC_BOUND',
                  'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(), 'valid': True,
                  'sdk_version_bound_bytes_assumed_for_pinned_build': 64,
                  'integer_scope': 'full 32/64-bit spellings; no small-value optimism',
                  'float_scope': 'finite full-double fixed-decimal bound including sign',
                  'records': records}, indent=2))
