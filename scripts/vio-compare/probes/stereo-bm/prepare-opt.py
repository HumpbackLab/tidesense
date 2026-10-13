#!/usr/bin/env python3
"""Generate an isolated OpenCV 3.4.20 StereoBM translation unit."""
import argparse
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
s = a.source.read_text()

def replace(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:90], s.count(old), count)
    s = s.replace(old, new)

replace('namespace cv\n{', '''// All optimizations are opt-in and independent.
#ifndef CV_STEREOBM_A53_STRIPE_ROWS
#define CV_STEREOBM_A53_STRIPE_ROWS 0
#endif
#ifndef CV_STEREOBM_A53_WIDE
#define CV_STEREOBM_A53_WIDE 0
#endif
#ifndef CV_STEREOBM_A53_RECOMPUTE
#define CV_STEREOBM_A53_RECOMPUTE 0
#endif

namespace cv
{''')
replace('            area.allocate(cbuf0[i], ((height + wsz + 2) * ndisp * (wsz + 2) + 256));', '''#if CV_STEREOBM_A53_RECOMPUTE && CV_SIMD
            // The scalar path still uses the difference ring.
            if (!params.useShorts())
#endif
                area.allocate(cbuf0[i], ((height + wsz + 2) * ndisp * (wsz + 2) + 256));''')
replace('#if CV_SIMD\ntemplate <typename dType>\nstatic void findStereoCorrespondenceBM_SIMD', '''#if CV_SIMD
#if CV_STEREOBM_A53_WIDE || CV_STEREOBM_A53_RECOMPUTE
static inline v_uint16 bmA53Update(v_uint16 sum, v_uint8 incoming, v_uint8 outgoing, bool high)
{
#if CV_STEREOBM_A53_WIDE && CV_NEON
    // Short-SAD path: cap <= 31, window <= 21. Horizontal sums including
    // the incoming column are <= 62*22; no saturation or wrapping occurs.
    if (high)
        return v_uint16(vsubw_high_u8(vaddw_high_u8(sum.val, incoming.val), outgoing.val));
    return v_uint16(vsubw_u8(vaddw_u8(sum.val, vget_low_u8(incoming.val)), vget_low_u8(outgoing.val)));
#else
    v_uint16 in = high ? v_expand_high(incoming) : v_expand_low(incoming);
    v_uint16 out = high ? v_expand_high(outgoing) : v_expand_low(outgoing);
    return v_reinterpret_as_u16(v_reinterpret_as_s16(sum + in) - v_reinterpret_as_s16(out));
#endif
}
#endif
template <typename dType>
static void findStereoCorrespondenceBM_SIMD''')

# Transform only the SIMD matcher, keeping the scalar implementation intact.
begin = s.index('static void findStereoCorrespondenceBM_SIMD')
end = s.index('\nstatic void\nfindStereoCorrespondenceBM(', begin)
prefix, body, suffix = s[:begin], s[begin:end], s[end:]
def sub(old, new, count=1):
    global body
    assert body.count(old) == count, (old[:90], body.count(old), count)
    body = body.replace(old, new)

sub('    uchar *cbuf;', '#if !CV_STEREOBM_A53_RECOMPUTE\n    uchar *cbuf;\n#endif')
sub('    int cstep = (height + dy0 + dy1)*ndisp;', '#if !CV_STEREOBM_A53_RECOMPUTE\n    int cstep = (height + dy0 + dy1)*ndisp;\n#endif')
sub('    uchar *cbuf0 = bufX.cbuf0[bufNum] + (wsz2 + 1) * ndisp;', '#if !CV_STEREOBM_A53_RECOMPUTE\n    uchar *cbuf0 = bufX.cbuf0[bufNum] + (wsz2 + 1) * ndisp;\n#endif')
sub('        hsad = hsad0 - dy0*ndisp; cbuf = cbuf0 + (x + wsz2 + 1)*cstep - dy0*ndisp;', '''        hsad = hsad0 - dy0*ndisp;
#if !CV_STEREOBM_A53_RECOMPUTE
        cbuf = cbuf0 + (x + wsz2 + 1)*cstep - dy0*ndisp;
#endif''')
sub('y++, hsad += ndisp, cbuf += ndisp, lptr += sstep, rptr += sstep', '''y++, hsad += ndisp,
#if !CV_STEREOBM_A53_RECOMPUTE
             cbuf += ndisp,
#endif
             lptr += sstep, rptr += sstep''')
# Initial ring writes are unnecessary when outgoing differences are recomputed.
for line in ['                v_store(cbuf + d, diff);', '                v_store_low(cbuf + d, diff);', '                cbuf[d] = (uchar)diff;']:
    sub(line, '#if !CV_STEREOBM_A53_RECOMPUTE\n' + line + '\n#endif', count=2)

sub('        const uchar* cbuf_sub = cbuf0 + ((x0 + wsz2 + 1) % (wsz + 1))*cstep - dy0*ndisp;\n        cbuf = cbuf0 + ((x1 + wsz2 + 1) % (wsz + 1))*cstep - dy0*ndisp;', '''#if !CV_STEREOBM_A53_RECOMPUTE
        const uchar* cbuf_sub = cbuf0 + ((x0 + wsz2 + 1) % (wsz + 1))*cstep - dy0*ndisp;
        cbuf = cbuf0 + ((x1 + wsz2 + 1) % (wsz + 1))*cstep - dy0*ndisp;
#else
        const uchar* rptr_sub = rptr0 + MIN(MAX(x0, -rofs), width-ndisp-rofs) - dy0*sstep;
#endif''')
sub('y++, cbuf += ndisp, cbuf_sub += ndisp,\n            hsad += ndisp, lptr += sstep, lptr_sub += sstep, rptr += sstep', '''y++,
#if !CV_STEREOBM_A53_RECOMPUTE
            cbuf += ndisp, cbuf_sub += ndisp,
#else
            rptr_sub += sstep,
#endif
            hsad += ndisp, lptr += sstep, lptr_sub += sstep, rptr += sstep''')
marker = '            int lval = lptr[0];\n            v_uint8 lv = vx_setall_u8((uchar)lval);'
# Only the second occurrence is the moving-window update.
pos = body.index(marker, body.index(marker) + len(marker))
body = body[:pos] + body[pos:].replace(marker, marker + '''
#if CV_STEREOBM_A53_RECOMPUTE
            v_uint8 outgoingValue = vx_setall_u8(lptr_sub[0]);
#endif''', 1)
old = '''                v_int8 cbs = v_reinterpret_as_s8(vx_load(cbuf_sub + d));
#if !CV_STEREOBM_A53_RECOMPUTE
                v_store(cbuf + d, diff);
#endif
                v_store(hsad + d, v_reinterpret_as_u16(v_reinterpret_as_s16(vx_load(hsad + d) + v_expand_low(diff)) - v_expand_low(cbs)));
                v_store(hsad + d + v_uint16::nlanes, v_reinterpret_as_u16(v_reinterpret_as_s16(vx_load(hsad + d + v_uint16::nlanes) + v_expand_high(diff)) - v_expand_high(cbs)));'''
sub(old, '''#if CV_STEREOBM_A53_RECOMPUTE || CV_STEREOBM_A53_WIDE
#if CV_STEREOBM_A53_RECOMPUTE
                v_uint8 outgoing = v_absdiff(outgoingValue, vx_load(rptr_sub + d));
#else
                v_uint8 outgoing = vx_load(cbuf_sub + d);
                v_store(cbuf + d, diff);
#endif
                v_store(hsad + d, bmA53Update(vx_load(hsad + d), diff, outgoing, false));
                v_store(hsad + d + v_uint16::nlanes,
                        bmA53Update(vx_load(hsad + d + v_uint16::nlanes), diff, outgoing, true));
#else
''' + old + '''
#endif''')
old = '''#if !CV_STEREOBM_A53_RECOMPUTE
                v_store_low(cbuf + d, diff);
#endif
                v_store(hsad + d, v_reinterpret_as_u16(v_reinterpret_as_s16(vx_load(hsad + d) + v_expand_low(diff)) - vx_load_expand((schar*)cbuf_sub + d)));'''
sub(old, '''#if CV_STEREOBM_A53_RECOMPUTE || CV_STEREOBM_A53_WIDE
#if CV_STEREOBM_A53_RECOMPUTE
                v_uint8 outgoing = v_absdiff(outgoingValue, vx_load_low(rptr_sub + d));
#else
                v_uint8 outgoing = vx_load_low(cbuf_sub + d);
                v_store_low(cbuf + d, diff);
#endif
                v_store(hsad + d, bmA53Update(vx_load(hsad + d), diff, outgoing, false));
#else
''' + old + '''
#endif''')
sub('                hsad[d] = hsad[d] + (ushort)diff - cbuf_sub[d];', '''#if CV_STEREOBM_A53_RECOMPUTE
                hsad[d] = hsad[d] + (ushort)diff - std::abs(int(lptr_sub[0]) - int(rptr_sub[d]));
#else
                hsad[d] = hsad[d] + (ushort)diff - cbuf_sub[d];
#endif''')
s = prefix + body + suffix

replace('        Mat left_i = left->rowRange(row0, row1);', '''#if CV_STEREOBM_A53_STRIPE_ROWS > 0
        const size_t bufferIndex = 0; // Serial stripes reuse one scratch buffer.
#else
        const size_t bufferIndex = range.start;
#endif
        Mat left_i = left->rowRange(row0, row1);''')
replace('buf, range.start', 'buf, bufferIndex', count=4)
replace('            const int nstripes = cvCeil(height / maxStripeSize);\n            BufferBM localBuf(nstripes, width, height, params);', '''#if CV_STEREOBM_A53_STRIPE_ROWS > 0
            const int nstripes = cvCeil(double(height) / CV_STEREOBM_A53_STRIPE_ROWS);
            BufferBM localBuf(1, width, cvCeil(double(height) / nstripes), params);
            CV_UNUSED(maxStripeSize);
#else
            const int nstripes = cvCeil(height / maxStripeSize);
            BufferBM localBuf(nstripes, width, height, params);
#endif''')
replace('            parallel_for_(Range(0, nstripes), invoker);', '''#if CV_STEREOBM_A53_STRIPE_ROWS > 0
            for (int stripe = 0; stripe < nstripes; ++stripe)
                invoker(Range(stripe, stripe + 1));
#else
            parallel_for_(Range(0, nstripes), invoker);
#endif''')
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(s)
print(a.output)
