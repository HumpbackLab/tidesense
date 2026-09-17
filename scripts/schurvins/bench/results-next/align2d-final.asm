00000000004adeb0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)>:
  4adeb0:	d10e03ff 	sub	sp, sp, #0x380
  4adeb4:	a9007bfd 	stp	x29, x30, [sp]
  4adeb8:	910003fd 	mov	x29, sp
  4adebc:	a9046bf9 	stp	x25, x26, [sp, #64]
  4adec0:	aa0003f9 	mov	x25, x0
  4adec4:	f941c3fa 	ldr	x26, [sp, #896]
  4adec8:	a90153f3 	stp	x19, x20, [sp, #16]
  4adecc:	12001cb4 	and	w20, w5, #0xff
  4aded0:	a9025bf5 	stp	x21, x22, [sp, #32]
  4aded4:	12001c95 	and	w21, w4, #0xff
  4aded8:	12001ce4 	and	w4, w7, #0xff
  4adedc:	a90363f7 	stp	x23, x24, [sp, #48]
  4adee0:	a90573fb 	stp	x27, x28, [sp, #80]
  4adee4:	aa0203fc 	mov	x28, x2
  4adee8:	6d0627e8 	stp	d8, d9, [sp, #96]
  4adeec:	6d072fea 	stp	d10, d11, [sp, #112]
  4adef0:	6d0837ec 	stp	d12, d13, [sp, #128]
  4adef4:	b40000ba 	cbz	x26, 4adf08 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x58>
  4adef8:	a9400b40 	ldp	x0, x2, [x26]
  4adefc:	eb00005f 	cmp	x2, x0
  4adf00:	54000040 	b.eq	4adf08 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x58>  // b.none
  4adf04:	f9000740 	str	x0, [x26, #8]
  4adf08:	4f000400 	movi	v0.4s, #0x0
  4adf0c:	2a140084 	orr	w4, w4, w20
  4adf10:	ad0803e0 	stp	q0, q0, [sp, #256]
  4adf14:	ad0903e0 	stp	q0, q0, [sp, #288]
  4adf18:	35000a04 	cbnz	w4, 4ae058 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1a8>
  4adf1c:	4f000406 	movi	v6.4s, #0x0
  4adf20:	910603ec 	add	x12, sp, #0x180
  4adf24:	4f000409 	movi	v9.4s, #0x0
  4adf28:	910a03e5 	add	x5, sp, #0x280
  4adf2c:	4f03f40d 	fmov	v13.4s, #5.000000000000000000e-01
  4adf30:	91003020 	add	x0, x1, #0xc
  4adf34:	aa0c03e7 	mov	x7, x12
  4adf38:	aa0503e2 	mov	x2, x5
  4adf3c:	4ea61cc7 	mov	v7.16b, v6.16b
  4adf40:	91017021 	add	x1, x1, #0x5c
  4adf44:	4ea91d2a 	mov	v10.16b, v9.16b
  4adf48:	4ea61cc8 	mov	v8.16b, v6.16b
  4adf4c:	d503201f 	nop
  4adf50:	fc5fe003 	ldur	d3, [x0, #-2]
  4adf54:	91002800 	add	x0, x0, #0xa
  4adf58:	fc5f6001 	ldur	d1, [x0, #-10]
  4adf5c:	fc5eb002 	ldur	d2, [x0, #-21]
  4adf60:	fc5ff000 	ldur	d0, [x0, #-1]
  4adf64:	eb01001f 	cmp	x0, x1
  4adf68:	2e232021 	usubl	v1.8h, v1.8b, v3.8b
  4adf6c:	2e222000 	usubl	v0.8h, v0.8b, v2.8b
  4adf70:	4f10a425 	sxtl2	v5.4s, v1.8h
  4adf74:	0f10a424 	sxtl	v4.4s, v1.4h
  4adf78:	4f10a402 	sxtl2	v2.4s, v0.8h
  4adf7c:	0f10a403 	sxtl	v3.4s, v0.4h
  4adf80:	4e21d8a5 	scvtf	v5.4s, v5.4s
  4adf84:	4e21d884 	scvtf	v4.4s, v4.4s
  4adf88:	4e21d842 	scvtf	v2.4s, v2.4s
  4adf8c:	4e21d863 	scvtf	v3.4s, v3.4s
  4adf90:	6e2ddca5 	fmul	v5.4s, v5.4s, v13.4s
  4adf94:	6e2ddc84 	fmul	v4.4s, v4.4s, v13.4s
  4adf98:	6e2ddc42 	fmul	v2.4s, v2.4s, v13.4s
  4adf9c:	6e2ddc63 	fmul	v3.4s, v3.4s, v13.4s
  4adfa0:	0e618028 	smlal	v8.4s, v1.4h, v1.4h
  4adfa4:	0e608027 	smlal	v7.4s, v1.4h, v0.4h
  4adfa8:	ad0014e4 	stp	q4, q5, [x7]
  4adfac:	910080e7 	add	x7, x7, #0x20
  4adfb0:	0e608006 	smlal	v6.4s, v0.4h, v0.4h
  4adfb4:	ad000843 	stp	q3, q2, [x2]
  4adfb8:	91008042 	add	x2, x2, #0x20
  4adfbc:	4e61854a 	add	v10.8h, v10.8h, v1.8h
  4adfc0:	4e618028 	smlal2	v8.4s, v1.8h, v1.8h
  4adfc4:	4e608529 	add	v9.8h, v9.8h, v0.8h
  4adfc8:	4e608027 	smlal2	v7.4s, v1.8h, v0.8h
  4adfcc:	4e608006 	smlal2	v6.4s, v0.8h, v0.8h
  4adfd0:	54fffc01 	b.ne	4adf50 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xa0>  // b.any
  4adfd4:	4eb1b902 	addv	s2, v8.4s
  4adfd8:	910433e0 	add	x0, sp, #0x10c
  4adfdc:	4eb1b8e1 	addv	s1, v7.4s
  4adfe0:	4eb1b8c0 	addv	s0, v6.4s
  4adfe4:	1e2a1003 	fmov	s3, #2.500000000000000000e-01
  4adfe8:	5e21d842 	scvtf	s2, s2
  4adfec:	5e21d821 	scvtf	s1, s1
  4adff0:	5e21d800 	scvtf	s0, s0
  4adff4:	1e230842 	fmul	s2, s2, s3
  4adff8:	1e230821 	fmul	s1, s1, s3
  4adffc:	1e230800 	fmul	s0, s0, s3
  4ae000:	2d3e8402 	stp	s2, s1, [x0, #-12]
  4ae004:	2d008001 	stp	s1, s0, [x0, #4]
  4ae008:	340028b5 	cbz	w21, 4ae51c <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x66c>
  4ae00c:	4e703941 	saddlv	s1, v10.8h
  4ae010:	52a85000 	mov	w0, #0x42800000            	// #1115684864
  4ae014:	4e703920 	saddlv	s0, v9.8h
  4ae018:	910503f6 	add	x22, sp, #0x140
  4ae01c:	1e2c1003 	fmov	s3, #5.000000000000000000e-01
  4ae020:	1e270002 	fmov	s2, w0
  4ae024:	910403e0 	add	x0, sp, #0x100
  4ae028:	5e21d821 	scvtf	s1, s1
  4ae02c:	bd012be2 	str	s2, [sp, #296]
  4ae030:	5e21d800 	scvtf	s0, s0
  4ae034:	1e230821 	fmul	s1, s1, s3
  4ae038:	1e230800 	fmul	s0, s0, s3
  4ae03c:	bd010be1 	str	s1, [sp, #264]
  4ae040:	bd011be0 	str	s0, [sp, #280]
  4ae044:	bd0123e1 	str	s1, [sp, #288]
  4ae048:	bd0127e0 	str	s0, [sp, #292]
  4ae04c:	1e2e1000 	fmov	s0, #1.000000000000000000e+00
  4ae050:	bd013fe0 	str	s0, [sp, #316]
  4ae054:	14000046 	b	4ae16c <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x2bc>
  4ae058:	0f000400 	movi	v0.2s, #0x0
  4ae05c:	710002bf 	cmp	w21, #0x0
  4ae060:	1e2e1007 	fmov	s7, #1.000000000000000000e+00
  4ae064:	910a03e5 	add	x5, sp, #0x280
  4ae068:	910603ec 	add	x12, sp, #0x180
  4ae06c:	91002c2f 	add	x15, x1, #0xb
  4ae070:	aa0503ee 	mov	x14, x5
  4ae074:	aa0c03ed 	mov	x13, x12
  4ae078:	1e201ce7 	fcsel	s7, s7, s0, ne  // ne = any
  4ae07c:	910383e2 	add	x2, sp, #0xe0
  4ae080:	910503f6 	add	x22, sp, #0x140
  4ae084:	910403e0 	add	x0, sp, #0x100
  4ae088:	1e6c1005 	fmov	d5, #5.000000000000000000e-01
  4ae08c:	d503201f 	nop
  4ae090:	aa0f03e1 	mov	x1, x15
  4ae094:	d2800007 	mov	x7, #0x0                   	// #0
  4ae098:	39400429 	ldrb	w9, [x1, #1]
  4ae09c:	0f000401 	movi	v1.2s, #0x0
  4ae0a0:	385ff02b 	ldurb	w11, [x1, #-1]
  4ae0a4:	39402828 	ldrb	w8, [x1, #10]
  4ae0a8:	385f602a 	ldurb	w10, [x1, #-10]
  4ae0ac:	4b0b0129 	sub	w9, w9, w11
  4ae0b0:	bd00ebe7 	str	s7, [sp, #232]
  4ae0b4:	4b0a0108 	sub	w8, w8, w10
  4ae0b8:	1e620120 	scvtf	d0, w9
  4ae0bc:	1e620102 	scvtf	d2, w8
  4ae0c0:	1e650800 	fmul	d0, d0, d5
  4ae0c4:	1e650842 	fmul	d2, d2, d5
  4ae0c8:	1e624000 	fcvt	s0, d0
  4ae0cc:	1e624042 	fcvt	s2, d2
  4ae0d0:	2d1c0be0 	stp	s0, s2, [sp, #224]
  4ae0d4:	340000b4 	cbz	w20, 4ae0e8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x238>
  4ae0d8:	39400028 	ldrb	w8, [x1]
  4ae0dc:	1e620101 	scvtf	d1, w8
  4ae0e0:	1e614021 	fneg	d1, d1
  4ae0e4:	1e624021 	fcvt	s1, d1
  4ae0e8:	bc2769a0 	str	s0, [x13, x7]
  4ae0ec:	91000421 	add	x1, x1, #0x1
  4ae0f0:	bc2769c2 	str	s2, [x14, x7]
  4ae0f4:	910010e7 	add	x7, x7, #0x4
  4ae0f8:	bd00efe1 	str	s1, [sp, #236]
  4ae0fc:	f10080ff 	cmp	x7, #0x20
  4ae100:	3dc00044 	ldr	q4, [x2]
  4ae104:	4f809080 	fmul	v0.4s, v4.4s, v0.s[0]
  4ae108:	4f819081 	fmul	v1.4s, v4.4s, v1.s[0]
  4ae10c:	3d8002c0 	str	q0, [x22]
  4ae110:	2d5c8be3 	ldp	s3, s2, [sp, #228]
  4ae114:	3d805fe1 	str	q1, [sp, #368]
  4ae118:	4f839083 	fmul	v3.4s, v4.4s, v3.s[0]
  4ae11c:	4f829082 	fmul	v2.4s, v4.4s, v2.s[0]
  4ae120:	ad0a8be3 	stp	q3, q2, [sp, #336]
  4ae124:	3dc00004 	ldr	q4, [x0]
  4ae128:	4e24d400 	fadd	v0.4s, v0.4s, v4.4s
  4ae12c:	3d800000 	str	q0, [x0]
  4ae130:	ad4893e6 	ldp	q6, q4, [sp, #272]
  4ae134:	3dc04fe0 	ldr	q0, [sp, #304]
  4ae138:	4e26d463 	fadd	v3.4s, v3.4s, v6.4s
  4ae13c:	4e24d442 	fadd	v2.4s, v2.4s, v4.4s
  4ae140:	4e20d421 	fadd	v1.4s, v1.4s, v0.4s
  4ae144:	ad088be3 	stp	q3, q2, [sp, #272]
  4ae148:	3d804fe1 	str	q1, [sp, #304]
  4ae14c:	54fffa61 	b.ne	4ae098 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1e8>  // b.any
  4ae150:	910081ad 	add	x13, x13, #0x20
  4ae154:	910081ce 	add	x14, x14, #0x20
  4ae158:	eb0501bf 	cmp	x13, x5
  4ae15c:	910029ef 	add	x15, x15, #0xa
  4ae160:	54fff981 	b.ne	4ae090 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1e0>  // b.any
  4ae164:	34001e15 	cbz	w21, 4ae524 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x674>
  4ae168:	34fff734 	cbz	w20, 4ae04c <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x19c>
  4ae16c:	aa1603e1 	mov	x1, x22
  4ae170:	29130fe4 	stp	w4, w3, [sp, #152]
  4ae174:	a90a17e6 	stp	x6, x5, [sp, #160]
  4ae178:	f9005bec 	str	x12, [sp, #176]
  4ae17c:	97fffe31 	bl	4ada40 <Eigen::internal::compute_inverse_size4<4, float, Eigen::Matrix<float, 4, 4, 0, 4, 4>, Eigen::Matrix<float, 4, 4, 0, 4, 4> >::run(Eigen::Matrix<float, 4, 4, 0, 4, 4> const&, Eigen::Matrix<float, 4, 4, 0, 4, 4>&)>
  4ae180:	a94a17e6 	ldp	x6, x5, [sp, #160]
  4ae184:	29530fe4 	ldp	w4, w3, [sp, #152]
  4ae188:	f9405bec 	ldr	x12, [sp, #176]
  4ae18c:	6d4028cb 	ldp	d11, d10, [x6]
  4ae190:	1e62416b 	fcvt	s11, d11
  4ae194:	1e62414a 	fcvt	s10, d10
  4ae198:	b400011a 	cbz	x26, 4ae1b8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x308>
  4ae19c:	a9408341 	ldp	x1, x0, [x26, #8]
  4ae1a0:	2d1c2beb 	stp	s11, s10, [sp, #224]
  4ae1a4:	eb00003f 	cmp	x1, x0
  4ae1a8:	54006f00 	b.eq	4aef88 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x10d8>  // b.none
  4ae1ac:	f94073e0 	ldr	x0, [sp, #224]
  4ae1b0:	f8008420 	str	x0, [x1], #8
  4ae1b4:	f9000741 	str	x1, [x26, #8]
  4ae1b8:	4f000404 	movi	v4.4s, #0x0
  4ae1bc:	7100007f 	cmp	w3, #0x0
  4ae1c0:	f9402720 	ldr	x0, [x25, #72]
  4ae1c4:	b940000b 	ldr	w11, [x0]
  4ae1c8:	3d8033e4 	str	q4, [sp, #192]
  4ae1cc:	54006f2d 	b.le	4aefb0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1100>
  4ae1d0:	93407d73 	sxtw	x19, w11
  4ae1d4:	0f000409 	movi	v9.2s, #0x0
  4ae1d8:	91000678 	add	x24, x19, #0x1
  4ae1dc:	529dbf40 	mov	w0, #0xedfa                	// #60922
  4ae1e0:	72a74d60 	movk	w0, #0x3a6b, lsl #16
  4ae1e4:	5280001b 	mov	w27, #0x0                   	// #0
  4ae1e8:	1e27000d 	fmov	s13, w0
  4ae1ec:	1e2e1008 	fmov	s8, #1.000000000000000000e+00
  4ae1f0:	1e6e100c 	fmov	d12, #1.000000000000000000e+00
  4ae1f4:	d503201f 	nop
  4ae1f8:	1e300161 	fcvtms	w1, s11
  4ae1fc:	1e300140 	fcvtms	w0, s10
  4ae200:	71000c3f 	cmp	w1, #0x3
  4ae204:	1a9fc7f7 	cset	w23, le
  4ae208:	71000c1f 	cmp	w0, #0x3
  4ae20c:	1a9fc7e2 	cset	w2, le
  4ae210:	2a0202f7 	orr	w23, w23, w2
  4ae214:	35006cf7 	cbnz	w23, 4aefb0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1100>
  4ae218:	b9400f22 	ldr	w2, [x25, #12]
  4ae21c:	51001042 	sub	w2, w2, #0x4
  4ae220:	6b01005f 	cmp	w2, w1
  4ae224:	540015ed 	b.le	4ae4e0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x630>
  4ae228:	b9400b22 	ldr	w2, [x25, #8]
  4ae22c:	51001042 	sub	w2, w2, #0x4
  4ae230:	6b00005f 	cmp	w2, w0
  4ae234:	5400156d 	b.le	4ae4e0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x630>
  4ae238:	1e2a2160 	fcmp	s11, s10
  4ae23c:	54001586 	b.vs	4ae4ec <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x63c>
  4ae240:	1e220027 	scvtf	s7, w1
  4ae244:	1e220000 	scvtf	s0, w0
  4ae248:	93407c22 	sxtw	x2, w1
  4ae24c:	3d8037e4 	str	q4, [sp, #208]
  4ae250:	d1001047 	sub	x7, x2, #0x4
  4ae254:	1e203940 	fsub	s0, s10, s0
  4ae258:	1e273967 	fsub	s7, s11, s7
  4ae25c:	1e22c006 	fcvt	d6, s0
  4ae260:	1e22c0e5 	fcvt	d5, s7
  4ae264:	1e2008e7 	fmul	s7, s7, s0
  4ae268:	1e653983 	fsub	d3, d12, d5
  4ae26c:	1e663980 	fsub	d0, d12, d6
  4ae270:	1e660866 	fmul	d6, d3, d6
  4ae274:	1e6008a5 	fmul	d5, d5, d0
  4ae278:	1e600863 	fmul	d3, d3, d0
  4ae27c:	1e6240a5 	fcvt	s5, d5
  4ae280:	1e6240c6 	fcvt	s6, d6
  4ae284:	1e624073 	fcvt	s19, d3
  4ae288:	35001a64 	cbnz	w4, 4ae5d4 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x724>
  4ae28c:	51001000 	sub	w0, w0, #0x4
  4ae290:	aa1c03ea 	mov	x10, x28
  4ae294:	4e0404a5 	dup	v5.4s, v5.s[0]
  4ae298:	aa0503e1 	mov	x1, x5
  4ae29c:	4e040663 	dup	v3.4s, v19.s[0]
  4ae2a0:	aa0c03e8 	mov	x8, x12
  4ae2a4:	1b0b7c00 	mul	w0, w0, w11
  4ae2a8:	910383e2 	add	x2, sp, #0xe0
  4ae2ac:	4e0404c6 	dup	v6.4s, v6.s[0]
  4ae2b0:	4e0404e7 	dup	v7.4s, v7.s[0]
  4ae2b4:	4e040511 	dup	v17.4s, v8.s[0]
  4ae2b8:	8b20c0e0 	add	x0, x7, w0, sxtw
  4ae2bc:	4e040530 	dup	v16.4s, v9.s[0]
  4ae2c0:	f9400b27 	ldr	x7, [x25, #16]
  4ae2c4:	d503201f 	nop
  4ae2c8:	8b0000e9 	add	x9, x7, x0
  4ae2cc:	fc6068e1 	ldr	d1, [x7, x0]
  4ae2d0:	fd400142 	ldr	d2, [x10]
  4ae2d4:	fc401120 	ldur	d0, [x9, #1]
  4ae2d8:	2f08a421 	uxtl	v1.8h, v1.8b
  4ae2dc:	fc736933 	ldr	d19, [x9, x19]
  4ae2e0:	2f08a442 	uxtl	v2.8h, v2.8b
  4ae2e4:	fc786932 	ldr	d18, [x9, x24]
  4ae2e8:	2f08a400 	uxtl	v0.8h, v0.8b
  4ae2ec:	2f08a673 	uxtl	v19.8h, v19.8b
  4ae2f0:	2f10a435 	uxtl	v21.4s, v1.4h
  4ae2f4:	6f10a434 	uxtl2	v20.4s, v1.8h
  4ae2f8:	2f10a401 	uxtl	v1.4s, v0.4h
  4ae2fc:	6f10a400 	uxtl2	v0.4s, v0.8h
  4ae300:	6e21dab5 	ucvtf	v21.4s, v21.4s
  4ae304:	2f10a676 	uxtl	v22.4s, v19.4h
  4ae308:	6e21d821 	ucvtf	v1.4s, v1.4s
  4ae30c:	6e21d800 	ucvtf	v0.4s, v0.4s
  4ae310:	2f08a652 	uxtl	v18.8h, v18.8b
  4ae314:	6e21da94 	ucvtf	v20.4s, v20.4s
  4ae318:	6e25dc21 	fmul	v1.4s, v1.4s, v5.4s
  4ae31c:	6f10a673 	uxtl2	v19.4s, v19.8h
  4ae320:	6e25dc00 	fmul	v0.4s, v0.4s, v5.4s
  4ae324:	6e21dad6 	ucvtf	v22.4s, v22.4s
  4ae328:	4e23cea1 	fmla	v1.4s, v21.4s, v3.4s
  4ae32c:	2f10a655 	uxtl	v21.4s, v18.4h
  4ae330:	6e21da73 	ucvtf	v19.4s, v19.4s
  4ae334:	4e23ce80 	fmla	v0.4s, v20.4s, v3.4s
  4ae338:	6f10a652 	uxtl2	v18.4s, v18.8h
  4ae33c:	2f10a454 	uxtl	v20.4s, v2.4h
  4ae340:	4e26cec1 	fmla	v1.4s, v22.4s, v6.4s
  4ae344:	6e21dab5 	ucvtf	v21.4s, v21.4s
  4ae348:	4e26ce60 	fmla	v0.4s, v19.4s, v6.4s
  4ae34c:	6e21da52 	ucvtf	v18.4s, v18.4s
  4ae350:	6f10a442 	uxtl2	v2.4s, v2.8h
  4ae354:	4e27cea1 	fmla	v1.4s, v21.4s, v7.4s
  4ae358:	6e21da93 	ucvtf	v19.4s, v20.4s
  4ae35c:	4e27ce40 	fmla	v0.4s, v18.4s, v7.4s
  4ae360:	6e21d842 	ucvtf	v2.4s, v2.4s
  4ae364:	4eb1ce61 	fmls	v1.4s, v19.4s, v17.4s
  4ae368:	4eb1cc40 	fmls	v0.4s, v2.4s, v17.4s
  4ae36c:	4e30d421 	fadd	v1.4s, v1.4s, v16.4s
  4ae370:	4e30d400 	fadd	v0.4s, v0.4s, v16.4s
  4ae374:	3d800041 	str	q1, [x2]
  4ae378:	bd40e3e2 	ldr	s2, [sp, #224]
  4ae37c:	3d803fe0 	str	q0, [sp, #240]
  4ae380:	bd400021 	ldr	s1, [x1]
  4ae384:	bd40d7e0 	ldr	s0, [sp, #212]
  4ae388:	bd400112 	ldr	s18, [x8]
  4ae38c:	1f018040 	fmsub	s0, s2, s1, s0
  4ae390:	bd40d3e1 	ldr	s1, [sp, #208]
  4ae394:	1f128441 	fmsub	s1, s2, s18, s1
  4ae398:	bd00d7e0 	str	s0, [sp, #212]
  4ae39c:	34000cb5 	cbz	w21, 4ae530 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x680>
  4ae3a0:	bd40dbf2 	ldr	s18, [sp, #216]
  4ae3a4:	910e03e9 	add	x9, sp, #0x380
  4ae3a8:	2d5ccff4 	ldp	s20, s19, [sp, #228]
  4ae3ac:	8b130000 	add	x0, x0, x19
  4ae3b0:	1e223a52 	fsub	s18, s18, s2
  4ae3b4:	bd400435 	ldr	s21, [x1, #4]
  4ae3b8:	bd400516 	ldr	s22, [x8, #4]
  4ae3bc:	9100214a 	add	x10, x10, #0x8
  4ae3c0:	bd400c38 	ldr	s24, [x1, #12]
  4ae3c4:	1f158282 	fmsub	s2, s20, s21, s0
  4ae3c8:	bd400835 	ldr	s21, [x1, #8]
  4ae3cc:	1f168681 	fmsub	s1, s20, s22, s1
  4ae3d0:	1e343a52 	fsub	s18, s18, s20
  4ae3d4:	2d415116 	ldp	s22, s20, [x8, #8]
  4ae3d8:	1f158a62 	fmsub	s2, s19, s21, s2
  4ae3dc:	bd401439 	ldr	s25, [x1, #20]
  4ae3e0:	2d5dd7e0 	ldp	s0, s21, [sp, #236]
  4ae3e4:	1f168661 	fmsub	s1, s19, s22, s1
  4ae3e8:	1e333a52 	fsub	s18, s18, s19
  4ae3ec:	bd401036 	ldr	s22, [x1, #16]
  4ae3f0:	2d426917 	ldp	s23, s26, [x8, #16]
  4ae3f4:	1f008b02 	fmsub	s2, s24, s0, s2
  4ae3f8:	1f008681 	fmsub	s1, s20, s0, s1
  4ae3fc:	bd40f7f4 	ldr	s20, [sp, #244]
  4ae400:	1e203a52 	fsub	s18, s18, s0
  4ae404:	bd40fbf3 	ldr	s19, [sp, #248]
  4ae408:	bd40ffe0 	ldr	s0, [sp, #252]
  4ae40c:	1f158ac2 	fmsub	s2, s22, s21, s2
  4ae410:	1f1786a1 	fmsub	s1, s21, s23, s1
  4ae414:	1e353a52 	fsub	s18, s18, s21
  4ae418:	2d435918 	ldp	s24, s22, [x8, #24]
  4ae41c:	1f148b22 	fmsub	s2, s25, s20, s2
  4ae420:	1f148741 	fmsub	s1, s26, s20, s1
  4ae424:	91008108 	add	x8, x8, #0x20
  4ae428:	2d435437 	ldp	s23, s21, [x1, #24]
  4ae42c:	1e343a52 	fsub	s18, s18, s20
  4ae430:	91008021 	add	x1, x1, #0x20
  4ae434:	1f188661 	fmsub	s1, s19, s24, s1
  4ae438:	eb09003f 	cmp	x1, x9
  4ae43c:	1f178a62 	fmsub	s2, s19, s23, s2
  4ae440:	1e333a52 	fsub	s18, s18, s19
  4ae444:	1f168401 	fmsub	s1, s0, s22, s1
  4ae448:	1f158802 	fmsub	s2, s0, s21, s2
  4ae44c:	1e203a52 	fsub	s18, s18, s0
  4ae450:	bd00dbf2 	str	s18, [sp, #216]
  4ae454:	2d1a0be1 	stp	s1, s2, [sp, #208]
  4ae458:	54fff381 	b.ne	4ae2c8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x418>  // b.any
  4ae45c:	b900dfff 	str	wzr, [sp, #220]
  4ae460:	3dc002c0 	ldr	q0, [x22]
  4ae464:	ad4a97e6 	ldp	q6, q5, [sp, #336]
  4ae468:	4f819000 	fmul	v0.4s, v0.4s, v1.s[0]
  4ae46c:	bd40dfe3 	ldr	s3, [sp, #220]
  4ae470:	3dc05fe1 	ldr	q1, [sp, #368]
  4ae474:	4f8210c0 	fmla	v0.4s, v6.4s, v2.s[0]
  4ae478:	4f9210a0 	fmla	v0.4s, v5.4s, v18.s[0]
  4ae47c:	4f831020 	fmla	v0.4s, v1.4s, v3.s[0]
  4ae480:	3d800040 	str	q0, [x2]
  4ae484:	3d8033e0 	str	q0, [sp, #192]
  4ae488:	2d5807e0 	ldp	s0, s1, [sp, #192]
  4ae48c:	2d590be3 	ldp	s3, s2, [sp, #200]
  4ae490:	1e20296b 	fadd	s11, s11, s0
  4ae494:	1e21294a 	fadd	s10, s10, s1
  4ae498:	1e232929 	fadd	s9, s9, s3
  4ae49c:	1e222908 	fadd	s8, s8, s2
  4ae4a0:	b400013a 	cbz	x26, 4ae4c4 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x614>
  4ae4a4:	a9408341 	ldp	x1, x0, [x26, #8]
  4ae4a8:	2d1c2beb 	stp	s11, s10, [sp, #224]
  4ae4ac:	eb00003f 	cmp	x1, x0
  4ae4b0:	54005540 	b.eq	4aef58 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x10a8>  // b.none
  4ae4b4:	f94073e0 	ldr	x0, [sp, #224]
  4ae4b8:	f8008420 	str	x0, [x1], #8
  4ae4bc:	f9000741 	str	x1, [x26, #8]
  4ae4c0:	2d5807e0 	ldp	s0, s1, [sp, #192]
  4ae4c4:	1e210821 	fmul	s1, s1, s1
  4ae4c8:	1f000400 	fmadd	s0, s0, s0, s1
  4ae4cc:	1e2d2010 	fcmpe	s0, s13
  4ae4d0:	54005744 	b.mi	4aefb8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1108>  // b.first
  4ae4d4:	1100077b 	add	w27, w27, #0x1
  4ae4d8:	6b1b007f 	cmp	w3, w27
  4ae4dc:	54ffe8e1 	b.ne	4ae1f8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x348>  // b.any
  4ae4e0:	1e22c16b 	fcvt	d11, s11
  4ae4e4:	1e22c14a 	fcvt	d10, s10
  4ae4e8:	6d0028cb 	stp	d11, d10, [x6]
  4ae4ec:	2a1703e0 	mov	w0, w23
  4ae4f0:	a9407bfd 	ldp	x29, x30, [sp]
  4ae4f4:	a94153f3 	ldp	x19, x20, [sp, #16]
  4ae4f8:	a9425bf5 	ldp	x21, x22, [sp, #32]
  4ae4fc:	a94363f7 	ldp	x23, x24, [sp, #48]
  4ae500:	a9446bf9 	ldp	x25, x26, [sp, #64]
  4ae504:	a94573fb 	ldp	x27, x28, [sp, #80]
  4ae508:	6d4627e8 	ldp	d8, d9, [sp, #96]
  4ae50c:	6d472fea 	ldp	d10, d11, [sp, #112]
  4ae510:	6d4837ec 	ldp	d12, d13, [sp, #128]
  4ae514:	910e03ff 	add	sp, sp, #0x380
  4ae518:	d65f03c0 	ret
  4ae51c:	910503f6 	add	x22, sp, #0x140
  4ae520:	910403e0 	add	x0, sp, #0x100
  4ae524:	1e2e1000 	fmov	s0, #1.000000000000000000e+00
  4ae528:	bd012be0 	str	s0, [sp, #296]
  4ae52c:	17ffff0f 	b	4ae168 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x2b8>
  4ae530:	2d5ccfe2 	ldp	s2, s19, [sp, #228]
  4ae534:	910e03e9 	add	x9, sp, #0x380
  4ae538:	bd400432 	ldr	s18, [x1, #4]
  4ae53c:	8b130000 	add	x0, x0, x19
  4ae540:	bd400514 	ldr	s20, [x8, #4]
  4ae544:	9100214a 	add	x10, x10, #0x8
  4ae548:	2d41d917 	ldp	s23, s22, [x8, #12]
  4ae54c:	1f148441 	fmsub	s1, s2, s20, s1
  4ae550:	1f128042 	fmsub	s2, s2, s18, s0
  4ae554:	bd400914 	ldr	s20, [x8, #8]
  4ae558:	bd400820 	ldr	s0, [x1, #8]
  4ae55c:	2d426035 	ldp	s21, s24, [x1, #16]
  4ae560:	1f148661 	fmsub	s1, s19, s20, s1
  4ae564:	1f138802 	fmsub	s2, s0, s19, s2
  4ae568:	bd400c34 	ldr	s20, [x1, #12]
  4ae56c:	2d5d83f2 	ldp	s18, s0, [sp, #236]
  4ae570:	91008021 	add	x1, x1, #0x20
  4ae574:	bd40f7f3 	ldr	s19, [sp, #244]
  4ae578:	1f178641 	fmsub	s1, s18, s23, s1
  4ae57c:	1f128a82 	fmsub	s2, s20, s18, s2
  4ae580:	2d42dd14 	ldp	s20, s23, [x8, #20]
  4ae584:	91008108 	add	x8, x8, #0x20
  4ae588:	1f168401 	fmsub	s1, s0, s22, s1
  4ae58c:	1f158802 	fmsub	s2, s0, s21, s2
  4ae590:	2d5f03f2 	ldp	s18, s0, [sp, #248]
  4ae594:	bc5f8036 	ldur	s22, [x1, #-8]
  4ae598:	1f148661 	fmsub	s1, s19, s20, s1
  4ae59c:	1f138b02 	fmsub	s2, s24, s19, s2
  4ae5a0:	bc5fc034 	ldur	s20, [x1, #-4]
  4ae5a4:	eb09003f 	cmp	x1, x9
  4ae5a8:	bc5fc115 	ldur	s21, [x8, #-4]
  4ae5ac:	1f178641 	fmsub	s1, s18, s23, s1
  4ae5b0:	1f128ac2 	fmsub	s2, s22, s18, s2
  4ae5b4:	1f158401 	fmsub	s1, s0, s21, s1
  4ae5b8:	1f148802 	fmsub	s2, s0, s20, s2
  4ae5bc:	2d1a0be1 	stp	s1, s2, [sp, #208]
  4ae5c0:	54ffe841 	b.ne	4ae2c8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x418>  // b.any
  4ae5c4:	b900dbff 	str	wzr, [sp, #216]
  4ae5c8:	bd40dbf2 	ldr	s18, [sp, #216]
  4ae5cc:	35fff4b4 	cbnz	w20, 4ae460 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x5b0>
  4ae5d0:	17ffffa3 	b	4ae45c <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x5ac>
  4ae5d4:	51001001 	sub	w1, w0, #0x4
  4ae5d8:	8b130048 	add	x8, x2, x19
  4ae5dc:	f9400b27 	ldr	x7, [x25, #16]
  4ae5e0:	d1000c4e 	sub	x14, x2, #0x3
  4ae5e4:	1b0b7c21 	mul	w1, w1, w11
  4ae5e8:	d100126d 	sub	x13, x19, #0x4
  4ae5ec:	91002389 	add	x9, x28, #0x8
  4ae5f0:	910683e0 	add	x0, sp, #0x1a0
  4ae5f4:	910a83ea 	add	x10, sp, #0x2a0
  4ae5f8:	91012390 	add	x16, x28, #0x48
  4ae5fc:	93407c21 	sxtw	x1, w1
  4ae600:	8b010042 	add	x2, x2, x1
  4ae604:	8b010108 	add	x8, x8, x1
  4ae608:	8b0201a2 	add	x2, x13, x2
  4ae60c:	8b0101c1 	add	x1, x14, x1
  4ae610:	8b0200e2 	add	x2, x7, x2
  4ae614:	8b0800e8 	add	x8, x7, x8
  4ae618:	8b0100e7 	add	x7, x7, x1
  4ae61c:	2d5a0be1 	ldp	s1, s2, [sp, #208]
  4ae620:	140000ac 	b	4ae8d0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xa20>
  4ae624:	3d4000e0 	ldr	b0, [x7]
  4ae628:	35002dd4 	cbnz	w20, 4aebe0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xd30>
  4ae62c:	3c5ff0f0 	ldur	b16, [x7, #-1]
  4ae630:	5e21d800 	scvtf	s0, s0
  4ae634:	3c5fc103 	ldur	b3, [x8, #-4]
  4ae638:	3c5fd11a 	ldur	b26, [x8, #-3]
  4ae63c:	5e21da10 	scvtf	s16, s16
  4ae640:	3c5f8137 	ldur	b23, [x9, #-8]
  4ae644:	1e250800 	fmul	s0, s0, s5
  4ae648:	5e21d863 	scvtf	s3, s3
  4ae64c:	5e21db5a 	scvtf	s26, s26
  4ae650:	3c5f9135 	ldur	b21, [x9, #-7]
  4ae654:	5e21daf7 	scvtf	s23, s23
  4ae658:	3c5fa131 	ldur	b17, [x9, #-6]
  4ae65c:	1f100260 	fmadd	s0, s19, s16, s0
  4ae660:	3c5fb136 	ldur	b22, [x9, #-5]
  4ae664:	2d7c4959 	ldp	s25, s18, [x10, #-32]
  4ae668:	5e21dab5 	scvtf	s21, s21
  4ae66c:	2d7c5018 	ldp	s24, s20, [x0, #-32]
  4ae670:	1f0300c0 	fmadd	s0, s6, s3, s0
  4ae674:	5e21da31 	scvtf	s17, s17
  4ae678:	bc5e8143 	ldur	s3, [x10, #-24]
  4ae67c:	2d7d6c10 	ldp	s16, s27, [x0, #-24]
  4ae680:	5e21dad6 	scvtf	s22, s22
  4ae684:	1f1a00e0 	fmadd	s0, s7, s26, s0
  4ae688:	1f178100 	fmsub	s0, s8, s23, s0
  4ae68c:	1e202920 	fadd	s0, s9, s0
  4ae690:	1f008718 	fmsub	s24, s24, s0, s1
  4ae694:	1f008b39 	fmsub	s25, s25, s0, s2
  4ae698:	2d1a67f8 	stp	s24, s25, [sp, #208]
  4ae69c:	3d4004e0 	ldr	b0, [x7, #1]
  4ae6a0:	3d4000f7 	ldr	b23, [x7]
  4ae6a4:	3c5fd102 	ldur	b2, [x8, #-3]
  4ae6a8:	5e21d800 	scvtf	s0, s0
  4ae6ac:	3c5fe101 	ldur	b1, [x8, #-2]
  4ae6b0:	5e21daf7 	scvtf	s23, s23
  4ae6b4:	5e21d842 	scvtf	s2, s2
  4ae6b8:	5e21d821 	scvtf	s1, s1
  4ae6bc:	1e250800 	fmul	s0, s0, s5
  4ae6c0:	1f170260 	fmadd	s0, s19, s23, s0
  4ae6c4:	1f0200c0 	fmadd	s0, s6, s2, s0
  4ae6c8:	1f0100e0 	fmadd	s0, s7, s1, s0
  4ae6cc:	1f158100 	fmsub	s0, s8, s21, s0
  4ae6d0:	1e202920 	fadd	s0, s9, s0
  4ae6d4:	1f00e298 	fmsub	s24, s20, s0, s24
  4ae6d8:	1f00e659 	fmsub	s25, s18, s0, s25
  4ae6dc:	2d1a67f8 	stp	s24, s25, [sp, #208]
  4ae6e0:	3d4008e0 	ldr	b0, [x7, #2]
  4ae6e4:	3d4004f2 	ldr	b18, [x7, #1]
  4ae6e8:	3c5fe102 	ldur	b2, [x8, #-2]
  4ae6ec:	5e21d800 	scvtf	s0, s0
  4ae6f0:	3c5ff101 	ldur	b1, [x8, #-1]
  4ae6f4:	5e21da52 	scvtf	s18, s18
  4ae6f8:	5e21d842 	scvtf	s2, s2
  4ae6fc:	5e21d821 	scvtf	s1, s1
  4ae700:	1e250800 	fmul	s0, s0, s5
  4ae704:	1f120260 	fmadd	s0, s19, s18, s0
  4ae708:	1f0200c0 	fmadd	s0, s6, s2, s0
  4ae70c:	1f0100e0 	fmadd	s0, s7, s1, s0
  4ae710:	1f118100 	fmsub	s0, s8, s17, s0
  4ae714:	1e202920 	fadd	s0, s9, s0
  4ae718:	1f00e479 	fmsub	s25, s3, s0, s25
  4ae71c:	1f00e218 	fmsub	s24, s16, s0, s24
  4ae720:	2d1a67f8 	stp	s24, s25, [sp, #208]
  4ae724:	3d400ce0 	ldr	b0, [x7, #3]
  4ae728:	3d4008e1 	ldr	b1, [x7, #2]
  4ae72c:	3c5ff102 	ldur	b2, [x8, #-1]
  4ae730:	5e21d800 	scvtf	s0, s0
  4ae734:	3d40011c 	ldr	b28, [x8]
  4ae738:	5e21d821 	scvtf	s1, s1
  4ae73c:	5e21d842 	scvtf	s2, s2
  4ae740:	5e21db9c 	scvtf	s28, s28
  4ae744:	1e250800 	fmul	s0, s0, s5
  4ae748:	1f010260 	fmadd	s0, s19, s1, s0
  4ae74c:	2d7dd15a 	ldp	s26, s20, [x10, #-20]
  4ae750:	3c5fc137 	ldur	b23, [x9, #-4]
  4ae754:	1f0200c0 	fmadd	s0, s6, s2, s0
  4ae758:	3c5fd132 	ldur	b18, [x9, #-3]
  4ae75c:	2d7e0415 	ldp	s21, s1, [x0, #-16]
  4ae760:	5e21daf7 	scvtf	s23, s23
  4ae764:	5e21da52 	scvtf	s18, s18
  4ae768:	3c5fe130 	ldur	b16, [x9, #-2]
  4ae76c:	1f1c00e0 	fmadd	s0, s7, s28, s0
  4ae770:	bc5f8003 	ldur	s3, [x0, #-8]
  4ae774:	2d7e8951 	ldp	s17, s2, [x10, #-12]
  4ae778:	5e21da10 	scvtf	s16, s16
  4ae77c:	1f168100 	fmsub	s0, s8, s22, s0
  4ae780:	1e292800 	fadd	s0, s0, s9
  4ae784:	1f00e378 	fmsub	s24, s27, s0, s24
  4ae788:	1f00e756 	fmsub	s22, s26, s0, s25
  4ae78c:	2d1a5bf8 	stp	s24, s22, [sp, #208]
  4ae790:	3d4010e0 	ldr	b0, [x7, #4]
  4ae794:	3d400cfb 	ldr	b27, [x7, #3]
  4ae798:	3d40011a 	ldr	b26, [x8]
  4ae79c:	5e21d800 	scvtf	s0, s0
  4ae7a0:	3d400519 	ldr	b25, [x8, #1]
  4ae7a4:	5e21db7b 	scvtf	s27, s27
  4ae7a8:	5e21db5a 	scvtf	s26, s26
  4ae7ac:	5e21db39 	scvtf	s25, s25
  4ae7b0:	1e250800 	fmul	s0, s0, s5
  4ae7b4:	1f130360 	fmadd	s0, s27, s19, s0
  4ae7b8:	1f060340 	fmadd	s0, s26, s6, s0
  4ae7bc:	1f070320 	fmadd	s0, s25, s7, s0
  4ae7c0:	1f0882e0 	fmsub	s0, s23, s8, s0
  4ae7c4:	1e292800 	fadd	s0, s0, s9
  4ae7c8:	1f00e2b5 	fmsub	s21, s21, s0, s24
  4ae7cc:	1f00da96 	fmsub	s22, s20, s0, s22
  4ae7d0:	2d1a5bf5 	stp	s21, s22, [sp, #208]
  4ae7d4:	3d4014e0 	ldr	b0, [x7, #5]
  4ae7d8:	3d4010f8 	ldr	b24, [x7, #4]
  4ae7dc:	3d400517 	ldr	b23, [x8, #1]
  4ae7e0:	5e21d800 	scvtf	s0, s0
  4ae7e4:	3d400914 	ldr	b20, [x8, #2]
  4ae7e8:	5e21db18 	scvtf	s24, s24
  4ae7ec:	5e21daf7 	scvtf	s23, s23
  4ae7f0:	5e21da94 	scvtf	s20, s20
  4ae7f4:	1e250800 	fmul	s0, s0, s5
  4ae7f8:	1f130300 	fmadd	s0, s24, s19, s0
  4ae7fc:	1f0602e0 	fmadd	s0, s23, s6, s0
  4ae800:	1f070280 	fmadd	s0, s20, s7, s0
  4ae804:	1f128100 	fmsub	s0, s8, s18, s0
  4ae808:	1e292800 	fadd	s0, s0, s9
  4ae80c:	1f01d401 	fmsub	s1, s0, s1, s21
  4ae810:	1f11d816 	fmsub	s22, s0, s17, s22
  4ae814:	2d1a5be1 	stp	s1, s22, [sp, #208]
  4ae818:	3d4018e0 	ldr	b0, [x7, #6]
  4ae81c:	3d4014f4 	ldr	b20, [x7, #5]
  4ae820:	3d400912 	ldr	b18, [x8, #2]
  4ae824:	5e21d800 	scvtf	s0, s0
  4ae828:	3d400d11 	ldr	b17, [x8, #3]
  4ae82c:	5e21da94 	scvtf	s20, s20
  4ae830:	5e21da52 	scvtf	s18, s18
  4ae834:	5e21da31 	scvtf	s17, s17
  4ae838:	1e250800 	fmul	s0, s0, s5
  4ae83c:	1f140260 	fmadd	s0, s19, s20, s0
  4ae840:	1f1200c0 	fmadd	s0, s6, s18, s0
  4ae844:	1f1100e0 	fmadd	s0, s7, s17, s0
  4ae848:	1f108100 	fmsub	s0, s8, s16, s0
  4ae84c:	1e202920 	fadd	s0, s9, s0
  4ae850:	1f038401 	fmsub	s1, s0, s3, s1
  4ae854:	1f02d803 	fmsub	s3, s0, s2, s22
  4ae858:	2d1a0fe1 	stp	s1, s3, [sp, #208]
  4ae85c:	3d401ce0 	ldr	b0, [x7, #7]
  4ae860:	3d4018f4 	ldr	b20, [x7, #6]
  4ae864:	3d400d12 	ldr	b18, [x8, #3]
  4ae868:	5e21d800 	scvtf	s0, s0
  4ae86c:	3d401111 	ldr	b17, [x8, #4]
  4ae870:	5e21da94 	scvtf	s20, s20
  4ae874:	3c5ff130 	ldur	b16, [x9, #-1]
  4ae878:	5e21da52 	scvtf	s18, s18
  4ae87c:	bc5fc015 	ldur	s21, [x0, #-4]
  4ae880:	5e21da31 	scvtf	s17, s17
  4ae884:	bc5fc142 	ldur	s2, [x10, #-4]
  4ae888:	1e250800 	fmul	s0, s0, s5
  4ae88c:	5e21da10 	scvtf	s16, s16
  4ae890:	1f140260 	fmadd	s0, s19, s20, s0
  4ae894:	1f1200c0 	fmadd	s0, s6, s18, s0
  4ae898:	1f1100e0 	fmadd	s0, s7, s17, s0
  4ae89c:	1f108100 	fmsub	s0, s8, s16, s0
  4ae8a0:	1e202920 	fadd	s0, s9, s0
  4ae8a4:	1f0086a1 	fmsub	s1, s21, s0, s1
  4ae8a8:	1f008c42 	fmsub	s2, s2, s0, s3
  4ae8ac:	2d1a0be1 	stp	s1, s2, [sp, #208]
  4ae8b0:	91002129 	add	x9, x9, #0x8
  4ae8b4:	91008000 	add	x0, x0, #0x20
  4ae8b8:	eb10013f 	cmp	x9, x16
  4ae8bc:	9100814a 	add	x10, x10, #0x20
  4ae8c0:	8b130108 	add	x8, x8, x19
  4ae8c4:	8b1300e7 	add	x7, x7, x19
  4ae8c8:	8b130042 	add	x2, x2, x19
  4ae8cc:	54001840 	b.eq	4aebd4 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xd24>  // b.none
  4ae8d0:	d100814f 	sub	x15, x10, #0x20
  4ae8d4:	d100800e 	sub	x14, x0, #0x20
  4ae8d8:	d100212d 	sub	x13, x9, #0x8
  4ae8dc:	d10004f1 	sub	x17, x7, #0x1
  4ae8e0:	34ffea35 	cbz	w21, 4ae624 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x774>
  4ae8e4:	35002e54 	cbnz	w20, 4aeeac <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xffc>
  4ae8e8:	3d4000e0 	ldr	b0, [x7]
  4ae8ec:	91002129 	add	x9, x9, #0x8
  4ae8f0:	3c5ff0f0 	ldur	b16, [x7, #-1]
  4ae8f4:	8b130042 	add	x2, x2, x19
  4ae8f8:	3c5fc103 	ldur	b3, [x8, #-4]
  4ae8fc:	5e21d800 	scvtf	s0, s0
  4ae900:	3c5fd11a 	ldur	b26, [x8, #-3]
  4ae904:	5e21da10 	scvtf	s16, s16
  4ae908:	3c5f0137 	ldur	b23, [x9, #-16]
  4ae90c:	5e21d863 	scvtf	s3, s3
  4ae910:	bd40dbf8 	ldr	s24, [sp, #216]
  4ae914:	5e21db5a 	scvtf	s26, s26
  4ae918:	3c5f1135 	ldur	b21, [x9, #-15]
  4ae91c:	1e250800 	fmul	s0, s0, s5
  4ae920:	5e21daf7 	scvtf	s23, s23
  4ae924:	1f100260 	fmadd	s0, s19, s16, s0
  4ae928:	3c5f2131 	ldur	b17, [x9, #-14]
  4ae92c:	2d7c4959 	ldp	s25, s18, [x10, #-32]
  4ae930:	5e21dab5 	scvtf	s21, s21
  4ae934:	2d7c5016 	ldp	s22, s20, [x0, #-32]
  4ae938:	1f0300c0 	fmadd	s0, s6, s3, s0
  4ae93c:	5e21da31 	scvtf	s17, s17
  4ae940:	bc5e8010 	ldur	s16, [x0, #-24]
  4ae944:	bc5e8143 	ldur	s3, [x10, #-24]
  4ae948:	1f1a00e0 	fmadd	s0, s7, s26, s0
  4ae94c:	1f178100 	fmsub	s0, s8, s23, s0
  4ae950:	1e202920 	fadd	s0, s9, s0
  4ae954:	1f168416 	fmsub	s22, s0, s22, s1
  4ae958:	1f198802 	fmsub	s2, s0, s25, s2
  4ae95c:	1e203b01 	fsub	s1, s24, s0
  4ae960:	bd00dbe1 	str	s1, [sp, #216]
  4ae964:	2d1a0bf6 	stp	s22, s2, [sp, #208]
  4ae968:	3d4004e0 	ldr	b0, [x7, #1]
  4ae96c:	3d4000f9 	ldr	b25, [x7]
  4ae970:	3c5fd118 	ldur	b24, [x8, #-3]
  4ae974:	5e21d800 	scvtf	s0, s0
  4ae978:	3c5fe117 	ldur	b23, [x8, #-2]
  4ae97c:	5e21db39 	scvtf	s25, s25
  4ae980:	5e21db18 	scvtf	s24, s24
  4ae984:	5e21daf7 	scvtf	s23, s23
  4ae988:	1e250800 	fmul	s0, s0, s5
  4ae98c:	1f190260 	fmadd	s0, s19, s25, s0
  4ae990:	1f1800c0 	fmadd	s0, s6, s24, s0
  4ae994:	1f1700e0 	fmadd	s0, s7, s23, s0
  4ae998:	1f158100 	fmsub	s0, s8, s21, s0
  4ae99c:	1e202920 	fadd	s0, s9, s0
  4ae9a0:	1f128802 	fmsub	s2, s0, s18, s2
  4ae9a4:	1f14d816 	fmsub	s22, s0, s20, s22
  4ae9a8:	1e203821 	fsub	s1, s1, s0
  4ae9ac:	bd00dbe1 	str	s1, [sp, #216]
  4ae9b0:	2d1a0bf6 	stp	s22, s2, [sp, #208]
  4ae9b4:	3d4008e0 	ldr	b0, [x7, #2]
  4ae9b8:	3d4004f5 	ldr	b21, [x7, #1]
  4ae9bc:	3c5fe114 	ldur	b20, [x8, #-2]
  4ae9c0:	5e21d800 	scvtf	s0, s0
  4ae9c4:	3c5ff112 	ldur	b18, [x8, #-1]
  4ae9c8:	5e21dab5 	scvtf	s21, s21
  4ae9cc:	5e21da94 	scvtf	s20, s20
  4ae9d0:	5e21da52 	scvtf	s18, s18
  4ae9d4:	1e250800 	fmul	s0, s0, s5
  4ae9d8:	1f150260 	fmadd	s0, s19, s21, s0
  4ae9dc:	1f1400c0 	fmadd	s0, s6, s20, s0
  4ae9e0:	1f1200e0 	fmadd	s0, s7, s18, s0
  4ae9e4:	1f118100 	fmsub	s0, s8, s17, s0
  4ae9e8:	1e202920 	fadd	s0, s9, s0
  4ae9ec:	1f038811 	fmsub	s17, s0, s3, s2
  4ae9f0:	1f10d816 	fmsub	s22, s0, s16, s22
  4ae9f4:	1e203821 	fsub	s1, s1, s0
  4ae9f8:	bd00dbe1 	str	s1, [sp, #216]
  4ae9fc:	2d1a47f6 	stp	s22, s17, [sp, #208]
  4aea00:	3d4008e2 	ldr	b2, [x7, #2]
  4aea04:	3d400ce0 	ldr	b0, [x7, #3]
  4aea08:	3c5ff117 	ldur	b23, [x8, #-1]
  4aea0c:	5e21d842 	scvtf	s2, s2
  4aea10:	3d40011b 	ldr	b27, [x8]
  4aea14:	5e21d800 	scvtf	s0, s0
  4aea18:	3c5f313a 	ldur	b26, [x9, #-13]
  4aea1c:	5e21daf7 	scvtf	s23, s23
  4aea20:	3c5f4135 	ldur	b21, [x9, #-12]
  4aea24:	5e21db7b 	scvtf	s27, s27
  4aea28:	3c5f5130 	ldur	b16, [x9, #-11]
  4aea2c:	5e21db5a 	scvtf	s26, s26
  4aea30:	bc5f4003 	ldur	s3, [x0, #-12]
  4aea34:	1e250800 	fmul	s0, s0, s5
  4aea38:	5e21dab5 	scvtf	s21, s21
  4aea3c:	1f020260 	fmadd	s0, s19, s2, s0
  4aea40:	5e21da10 	scvtf	s16, s16
  4aea44:	2d7dd158 	ldp	s24, s20, [x10, #-20]
  4aea48:	2d7dc819 	ldp	s25, s18, [x0, #-20]
  4aea4c:	1f1700c0 	fmadd	s0, s6, s23, s0
  4aea50:	bc5f4142 	ldur	s2, [x10, #-12]
  4aea54:	1f1b00e0 	fmadd	s0, s7, s27, s0
  4aea58:	1f1a8100 	fmsub	s0, s8, s26, s0
  4aea5c:	1e202920 	fadd	s0, s9, s0
  4aea60:	1f19d816 	fmsub	s22, s0, s25, s22
  4aea64:	1f18c411 	fmsub	s17, s0, s24, s17
  4aea68:	1e203821 	fsub	s1, s1, s0
  4aea6c:	bd00dbe1 	str	s1, [sp, #216]
  4aea70:	2d1a47f6 	stp	s22, s17, [sp, #208]
  4aea74:	3d4010e0 	ldr	b0, [x7, #4]
  4aea78:	3d400cf9 	ldr	b25, [x7, #3]
  4aea7c:	3d400118 	ldr	b24, [x8]
  4aea80:	5e21d800 	scvtf	s0, s0
  4aea84:	3d400517 	ldr	b23, [x8, #1]
  4aea88:	5e21db39 	scvtf	s25, s25
  4aea8c:	5e21db18 	scvtf	s24, s24
  4aea90:	5e21daf7 	scvtf	s23, s23
  4aea94:	1e250800 	fmul	s0, s0, s5
  4aea98:	1f190260 	fmadd	s0, s19, s25, s0
  4aea9c:	1f1800c0 	fmadd	s0, s6, s24, s0
  4aeaa0:	1f1700e0 	fmadd	s0, s7, s23, s0
  4aeaa4:	1f158100 	fmsub	s0, s8, s21, s0
  4aeaa8:	1e202920 	fadd	s0, s9, s0
  4aeaac:	1f14c411 	fmsub	s17, s0, s20, s17
  4aeab0:	1f12d812 	fmsub	s18, s0, s18, s22
  4aeab4:	1e203820 	fsub	s0, s1, s0
  4aeab8:	bd00dbe0 	str	s0, [sp, #216]
  4aeabc:	2d1a47f2 	stp	s18, s17, [sp, #208]
  4aeac0:	3d4014e1 	ldr	b1, [x7, #5]
  4aeac4:	3d4010f6 	ldr	b22, [x7, #4]
  4aeac8:	3d400515 	ldr	b21, [x8, #1]
  4aeacc:	5e21d821 	scvtf	s1, s1
  4aead0:	3d400914 	ldr	b20, [x8, #2]
  4aead4:	5e21dad6 	scvtf	s22, s22
  4aead8:	5e21dab5 	scvtf	s21, s21
  4aeadc:	5e21da94 	scvtf	s20, s20
  4aeae0:	1e250821 	fmul	s1, s1, s5
  4aeae4:	1f160661 	fmadd	s1, s19, s22, s1
  4aeae8:	1f1504c1 	fmadd	s1, s6, s21, s1
  4aeaec:	1f1404e1 	fmadd	s1, s7, s20, s1
  4aeaf0:	1f108501 	fmsub	s1, s8, s16, s1
  4aeaf4:	1e212921 	fadd	s1, s9, s1
  4aeaf8:	1f03c832 	fmsub	s18, s1, s3, s18
  4aeafc:	1f02c430 	fmsub	s16, s1, s2, s17
  4aeb00:	1e213800 	fsub	s0, s0, s1
  4aeb04:	bd00dbe0 	str	s0, [sp, #216]
  4aeb08:	2d1a43f2 	stp	s18, s16, [sp, #208]
  4aeb0c:	3d4018e3 	ldr	b3, [x7, #6]
  4aeb10:	3d4014e1 	ldr	b1, [x7, #5]
  4aeb14:	3d400916 	ldr	b22, [x8, #2]
  4aeb18:	5e21d863 	scvtf	s3, s3
  4aeb1c:	3d400d15 	ldr	b21, [x8, #3]
  4aeb20:	5e21d821 	scvtf	s1, s1
  4aeb24:	3c5f6131 	ldur	b17, [x9, #-10]
  4aeb28:	5e21dad6 	scvtf	s22, s22
  4aeb2c:	3c5f7134 	ldur	b20, [x9, #-9]
  4aeb30:	5e21dab5 	scvtf	s21, s21
  4aeb34:	eb10013f 	cmp	x9, x16
  4aeb38:	1e250863 	fmul	s3, s3, s5
  4aeb3c:	5e21da31 	scvtf	s17, s17
  4aeb40:	1f010e63 	fmadd	s3, s19, s1, s3
  4aeb44:	5e21da94 	scvtf	s20, s20
  4aeb48:	2d7f0418 	ldp	s24, s1, [x0, #-8]
  4aeb4c:	91008000 	add	x0, x0, #0x20
  4aeb50:	2d7f0957 	ldp	s23, s2, [x10, #-8]
  4aeb54:	1f160cc3 	fmadd	s3, s6, s22, s3
  4aeb58:	9100814a 	add	x10, x10, #0x20
  4aeb5c:	1f150ce3 	fmadd	s3, s7, s21, s3
  4aeb60:	1f118d03 	fmsub	s3, s8, s17, s3
  4aeb64:	1e232923 	fadd	s3, s9, s3
  4aeb68:	1f18c871 	fmsub	s17, s3, s24, s18
  4aeb6c:	1f17c070 	fmsub	s16, s3, s23, s16
  4aeb70:	1e233800 	fsub	s0, s0, s3
  4aeb74:	bd00dbe0 	str	s0, [sp, #216]
  4aeb78:	2d1a43f1 	stp	s17, s16, [sp, #208]
  4aeb7c:	3d401ce3 	ldr	b3, [x7, #7]
  4aeb80:	3d4018f6 	ldr	b22, [x7, #6]
  4aeb84:	8b1300e7 	add	x7, x7, x19
  4aeb88:	3d400d15 	ldr	b21, [x8, #3]
  4aeb8c:	5e21d863 	scvtf	s3, s3
  4aeb90:	3d401112 	ldr	b18, [x8, #4]
  4aeb94:	5e21dad6 	scvtf	s22, s22
  4aeb98:	8b130108 	add	x8, x8, x19
  4aeb9c:	5e21dab5 	scvtf	s21, s21
  4aeba0:	5e21da52 	scvtf	s18, s18
  4aeba4:	1e250863 	fmul	s3, s3, s5
  4aeba8:	1f160e63 	fmadd	s3, s19, s22, s3
  4aebac:	1f150cc3 	fmadd	s3, s6, s21, s3
  4aebb0:	1f120ce3 	fmadd	s3, s7, s18, s3
  4aebb4:	1f148d03 	fmsub	s3, s8, s20, s3
  4aebb8:	1e232923 	fadd	s3, s9, s3
  4aebbc:	1f03c421 	fmsub	s1, s1, s3, s17
  4aebc0:	1f03c042 	fmsub	s2, s2, s3, s16
  4aebc4:	1e233800 	fsub	s0, s0, s3
  4aebc8:	bd00dbe0 	str	s0, [sp, #216]
  4aebcc:	2d1a0be1 	stp	s1, s2, [sp, #208]
  4aebd0:	54ffe801 	b.ne	4ae8d0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xa20>  // b.any
  4aebd4:	910383e2 	add	x2, sp, #0xe0
  4aebd8:	34ffcf75 	cbz	w21, 4ae5c4 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x714>
  4aebdc:	17fffe7b 	b	4ae5c8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x718>
  4aebe0:	3c5ff0e3 	ldur	b3, [x7, #-1]
  4aebe4:	5e21d800 	scvtf	s0, s0
  4aebe8:	3c5fc110 	ldur	b16, [x8, #-4]
  4aebec:	3c5fd11a 	ldur	b26, [x8, #-3]
  4aebf0:	5e21d863 	scvtf	s3, s3
  4aebf4:	3c5f8138 	ldur	b24, [x9, #-8]
  4aebf8:	1e250800 	fmul	s0, s0, s5
  4aebfc:	5e21da10 	scvtf	s16, s16
  4aec00:	5e21db5a 	scvtf	s26, s26
  4aec04:	bd40dff2 	ldr	s18, [sp, #220]
  4aec08:	5e21db18 	scvtf	s24, s24
  4aec0c:	3c5f9136 	ldur	b22, [x9, #-7]
  4aec10:	1f030260 	fmadd	s0, s19, s3, s0
  4aec14:	3c5fa123 	ldur	b3, [x9, #-6]
  4aec18:	2d7c5159 	ldp	s25, s20, [x10, #-32]
  4aec1c:	5e21dad6 	scvtf	s22, s22
  4aec20:	2d7c5417 	ldp	s23, s21, [x0, #-32]
  4aec24:	1f1000c0 	fmadd	s0, s6, s16, s0
  4aec28:	5e21d863 	scvtf	s3, s3
  4aec2c:	bc5e8150 	ldur	s16, [x10, #-24]
  4aec30:	bc5e8011 	ldur	s17, [x0, #-24]
  4aec34:	1f1a00e0 	fmadd	s0, s7, s26, s0
  4aec38:	1f188100 	fmsub	s0, s8, s24, s0
  4aec3c:	1e202920 	fadd	s0, s9, s0
  4aec40:	1f178417 	fmsub	s23, s0, s23, s1
  4aec44:	1f004b12 	fmadd	s18, s24, s0, s18
  4aec48:	1f198801 	fmsub	s1, s0, s25, s2
  4aec4c:	bd00dff2 	str	s18, [sp, #220]
  4aec50:	2d1a07f7 	stp	s23, s1, [sp, #208]
  4aec54:	3d4004e0 	ldr	b0, [x7, #1]
  4aec58:	3d4000f9 	ldr	b25, [x7]
  4aec5c:	3c5fd118 	ldur	b24, [x8, #-3]
  4aec60:	5e21d800 	scvtf	s0, s0
  4aec64:	3c5fe102 	ldur	b2, [x8, #-2]
  4aec68:	5e21db39 	scvtf	s25, s25
  4aec6c:	5e21db18 	scvtf	s24, s24
  4aec70:	5e21d842 	scvtf	s2, s2
  4aec74:	1e250800 	fmul	s0, s0, s5
  4aec78:	1f190260 	fmadd	s0, s19, s25, s0
  4aec7c:	1f1800c0 	fmadd	s0, s6, s24, s0
  4aec80:	1f0200e0 	fmadd	s0, s7, s2, s0
  4aec84:	1f168100 	fmsub	s0, s8, s22, s0
  4aec88:	1e202920 	fadd	s0, s9, s0
  4aec8c:	1f15dc02 	fmsub	s2, s0, s21, s23
  4aec90:	1f004ad2 	fmadd	s18, s22, s0, s18
  4aec94:	1f148401 	fmsub	s1, s0, s20, s1
  4aec98:	bd00dff2 	str	s18, [sp, #220]
  4aec9c:	2d1a07e2 	stp	s2, s1, [sp, #208]
  4aeca0:	3d4008e0 	ldr	b0, [x7, #2]
  4aeca4:	3d4004f6 	ldr	b22, [x7, #1]
  4aeca8:	3c5fe115 	ldur	b21, [x8, #-2]
  4aecac:	5e21d800 	scvtf	s0, s0
  4aecb0:	3c5ff114 	ldur	b20, [x8, #-1]
  4aecb4:	5e21dad6 	scvtf	s22, s22
  4aecb8:	5e21dab5 	scvtf	s21, s21
  4aecbc:	5e21da94 	scvtf	s20, s20
  4aecc0:	1e250800 	fmul	s0, s0, s5
  4aecc4:	1f160260 	fmadd	s0, s19, s22, s0
  4aecc8:	1f1500c0 	fmadd	s0, s6, s21, s0
  4aeccc:	1f1400e0 	fmadd	s0, s7, s20, s0
  4aecd0:	1f038100 	fmsub	s0, s8, s3, s0
  4aecd4:	1e202920 	fadd	s0, s9, s0
  4aecd8:	1f004872 	fmadd	s18, s3, s0, s18
  4aecdc:	1f118811 	fmsub	s17, s0, s17, s2
  4aece0:	1f108401 	fmsub	s1, s0, s16, s1
  4aece4:	bd00dff2 	str	s18, [sp, #220]
  4aece8:	2d1a07f1 	stp	s17, s1, [sp, #208]
  4aecec:	3d400ce0 	ldr	b0, [x7, #3]
  4aecf0:	3d4008e2 	ldr	b2, [x7, #2]
  4aecf4:	3c5ff117 	ldur	b23, [x8, #-1]
  4aecf8:	5e21d800 	scvtf	s0, s0
  4aecfc:	3d40011b 	ldr	b27, [x8]
  4aed00:	5e21d842 	scvtf	s2, s2
  4aed04:	3c5fb13a 	ldur	b26, [x9, #-5]
  4aed08:	5e21daf7 	scvtf	s23, s23
  4aed0c:	3c5fc136 	ldur	b22, [x9, #-4]
  4aed10:	5e21db7b 	scvtf	s27, s27
  4aed14:	bc5f4010 	ldur	s16, [x0, #-12]
  4aed18:	1e250800 	fmul	s0, s0, s5
  4aed1c:	5e21db5a 	scvtf	s26, s26
  4aed20:	1f020260 	fmadd	s0, s19, s2, s0
  4aed24:	5e21dad6 	scvtf	s22, s22
  4aed28:	2d7dd019 	ldp	s25, s20, [x0, #-20]
  4aed2c:	2d7dd558 	ldp	s24, s21, [x10, #-20]
  4aed30:	1f1700c0 	fmadd	s0, s6, s23, s0
  4aed34:	3c5fd122 	ldur	b2, [x9, #-3]
  4aed38:	bc5f4143 	ldur	s3, [x10, #-12]
  4aed3c:	1f1b00e0 	fmadd	s0, s7, s27, s0
  4aed40:	5e21d842 	scvtf	s2, s2
  4aed44:	1f1a8100 	fmsub	s0, s8, s26, s0
  4aed48:	1e202920 	fadd	s0, s9, s0
  4aed4c:	1f188401 	fmsub	s1, s0, s24, s1
  4aed50:	1f19c411 	fmsub	s17, s0, s25, s17
  4aed54:	1f004b52 	fmadd	s18, s26, s0, s18
  4aed58:	2d1a07f1 	stp	s17, s1, [sp, #208]
  4aed5c:	bd00dff2 	str	s18, [sp, #220]
  4aed60:	3d4010e0 	ldr	b0, [x7, #4]
  4aed64:	3d400cf9 	ldr	b25, [x7, #3]
  4aed68:	3d400118 	ldr	b24, [x8]
  4aed6c:	5e21d800 	scvtf	s0, s0
  4aed70:	3d400517 	ldr	b23, [x8, #1]
  4aed74:	5e21db39 	scvtf	s25, s25
  4aed78:	5e21db18 	scvtf	s24, s24
  4aed7c:	5e21daf7 	scvtf	s23, s23
  4aed80:	1e250800 	fmul	s0, s0, s5
  4aed84:	1f190260 	fmadd	s0, s19, s25, s0
  4aed88:	1f1800c0 	fmadd	s0, s6, s24, s0
  4aed8c:	1f1700e0 	fmadd	s0, s7, s23, s0
  4aed90:	1f168100 	fmsub	s0, s8, s22, s0
  4aed94:	1e202920 	fadd	s0, s9, s0
  4aed98:	1f004ad2 	fmadd	s18, s22, s0, s18
  4aed9c:	1f14c411 	fmsub	s17, s0, s20, s17
  4aeda0:	1f158400 	fmsub	s0, s0, s21, s1
  4aeda4:	bd00dff2 	str	s18, [sp, #220]
  4aeda8:	2d1a03f1 	stp	s17, s0, [sp, #208]
  4aedac:	3d4014e1 	ldr	b1, [x7, #5]
  4aedb0:	3d4010f6 	ldr	b22, [x7, #4]
  4aedb4:	3d400515 	ldr	b21, [x8, #1]
  4aedb8:	5e21d821 	scvtf	s1, s1
  4aedbc:	3d400914 	ldr	b20, [x8, #2]
  4aedc0:	5e21dad6 	scvtf	s22, s22
  4aedc4:	5e21dab5 	scvtf	s21, s21
  4aedc8:	5e21da94 	scvtf	s20, s20
  4aedcc:	1e250821 	fmul	s1, s1, s5
  4aedd0:	1f160661 	fmadd	s1, s19, s22, s1
  4aedd4:	1f1504c1 	fmadd	s1, s6, s21, s1
  4aedd8:	1f1404e1 	fmadd	s1, s7, s20, s1
  4aeddc:	1f028501 	fmsub	s1, s8, s2, s1
  4aede0:	1e212921 	fadd	s1, s9, s1
  4aede4:	1f014855 	fmadd	s21, s2, s1, s18
  4aede8:	1f038020 	fmsub	s0, s1, s3, s0
  4aedec:	1f10c432 	fmsub	s18, s1, s16, s17
  4aedf0:	bd00dff5 	str	s21, [sp, #220]
  4aedf4:	2d1a03f2 	stp	s18, s0, [sp, #208]
  4aedf8:	3d4018e3 	ldr	b3, [x7, #6]
  4aedfc:	3d4014e1 	ldr	b1, [x7, #5]
  4aee00:	3d400916 	ldr	b22, [x8, #2]
  4aee04:	5e21d863 	scvtf	s3, s3
  4aee08:	3d400d10 	ldr	b16, [x8, #3]
  4aee0c:	5e21d821 	scvtf	s1, s1
  4aee10:	3c5fe131 	ldur	b17, [x9, #-2]
  4aee14:	5e21dad6 	scvtf	s22, s22
  4aee18:	3c5ff134 	ldur	b20, [x9, #-1]
  4aee1c:	5e21da10 	scvtf	s16, s16
  4aee20:	1e250863 	fmul	s3, s3, s5
  4aee24:	5e21da31 	scvtf	s17, s17
  4aee28:	1f010e63 	fmadd	s3, s19, s1, s3
  4aee2c:	5e21da94 	scvtf	s20, s20
  4aee30:	2d7f0418 	ldp	s24, s1, [x0, #-8]
  4aee34:	2d7f0957 	ldp	s23, s2, [x10, #-8]
  4aee38:	1f160cc3 	fmadd	s3, s6, s22, s3
  4aee3c:	1f100ce3 	fmadd	s3, s7, s16, s3
  4aee40:	1f118d03 	fmsub	s3, s8, s17, s3
  4aee44:	1e232923 	fadd	s3, s9, s3
  4aee48:	1f035630 	fmadd	s16, s17, s3, s21
  4aee4c:	1f178060 	fmsub	s0, s3, s23, s0
  4aee50:	1f18c871 	fmsub	s17, s3, s24, s18
  4aee54:	bd00dff0 	str	s16, [sp, #220]
  4aee58:	2d1a03f1 	stp	s17, s0, [sp, #208]
  4aee5c:	3d401ce3 	ldr	b3, [x7, #7]
  4aee60:	3d4018f6 	ldr	b22, [x7, #6]
  4aee64:	3d400d15 	ldr	b21, [x8, #3]
  4aee68:	5e21d863 	scvtf	s3, s3
  4aee6c:	3d401112 	ldr	b18, [x8, #4]
  4aee70:	5e21dad6 	scvtf	s22, s22
  4aee74:	5e21dab5 	scvtf	s21, s21
  4aee78:	5e21da52 	scvtf	s18, s18
  4aee7c:	1e250863 	fmul	s3, s3, s5
  4aee80:	1f160e63 	fmadd	s3, s19, s22, s3
  4aee84:	1f150cc3 	fmadd	s3, s6, s21, s3
  4aee88:	1f120ce3 	fmadd	s3, s7, s18, s3
  4aee8c:	1f148d03 	fmsub	s3, s8, s20, s3
  4aee90:	1e232923 	fadd	s3, s9, s3
  4aee94:	1f144070 	fmadd	s16, s3, s20, s16
  4aee98:	1f03c421 	fmsub	s1, s1, s3, s17
  4aee9c:	1f038042 	fmsub	s2, s2, s3, s0
  4aeea0:	bd00dff0 	str	s16, [sp, #220]
  4aeea4:	2d1a0be1 	stp	s1, s2, [sp, #208]
  4aeea8:	17fffe82 	b	4ae8b0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xa00>
  4aeeac:	4ea11c34 	mov	v20.16b, v1.16b
  4aeeb0:	91000452 	add	x18, x2, #0x1
  4aeeb4:	2d5b03e3 	ldp	s3, s0, [sp, #216]
  4aeeb8:	d2800001 	mov	x1, #0x0                   	// #0
  4aeebc:	6e0c0454 	mov	v20.s[1], v2.s[0]
  4aeec0:	6e140474 	mov	v20.s[2], v3.s[0]
  4aeec4:	6e1c0414 	mov	v20.s[3], v0.s[0]
  4aeec8:	3c6168e0 	ldr	b0, [x7, x1]
  4aeecc:	3c616a36 	ldr	b22, [x17, x1]
  4aeed0:	3c616855 	ldr	b21, [x2, x1]
  4aeed4:	5e21d800 	scvtf	s0, s0
  4aeed8:	3c616a43 	ldr	b3, [x18, x1]
  4aeedc:	5e21dad6 	scvtf	s22, s22
  4aeee0:	3c6169b0 	ldr	b16, [x13, x1]
  4aeee4:	5e21dab5 	scvtf	s21, s21
  4aeee8:	bc6179d2 	ldr	s18, [x14, x1, lsl #2]
  4aeeec:	5e21d863 	scvtf	s3, s3
  4aeef0:	bc6179f1 	ldr	s17, [x15, x1, lsl #2]
  4aeef4:	1e250800 	fmul	s0, s0, s5
  4aeef8:	5e21da10 	scvtf	s16, s16
  4aeefc:	1f1302c0 	fmadd	s0, s22, s19, s0
  4aef00:	91000421 	add	x1, x1, #0x1
  4aef04:	f100203f 	cmp	x1, #0x8
  4aef08:	1f0602a0 	fmadd	s0, s21, s6, s0
  4aef0c:	1f070060 	fmadd	s0, s3, s7, s0
  4aef10:	1f088200 	fmsub	s0, s16, s8, s0
  4aef14:	1e292800 	fadd	s0, s0, s9
  4aef18:	1e320812 	fmul	s18, s0, s18
  4aef1c:	1e310811 	fmul	s17, s0, s17
  4aef20:	1e200a10 	fmul	s16, s16, s0
  4aef24:	4eb21e43 	mov	v3.16b, v18.16b
  4aef28:	1e323821 	fsub	s1, s1, s18
  4aef2c:	1e313842 	fsub	s2, s2, s17
  4aef30:	6e0c0623 	mov	v3.s[1], v17.s[0]
  4aef34:	6e140403 	mov	v3.s[2], v0.s[0]
  4aef38:	6e1c0603 	mov	v3.s[3], v16.s[0]
  4aef3c:	4ea3d680 	fsub	v0.4s, v20.4s, v3.4s
  4aef40:	4e34d463 	fadd	v3.4s, v3.4s, v20.4s
  4aef44:	6e1c6460 	mov	v0.s[3], v3.s[3]
  4aef48:	4ea01c14 	mov	v20.16b, v0.16b
  4aef4c:	3d8037e0 	str	q0, [sp, #208]
  4aef50:	54fffbc1 	b.ne	4aeec8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x1018>  // b.any
  4aef54:	17fffe57 	b	4ae8b0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0xa00>
  4aef58:	aa1a03e0 	mov	x0, x26
  4aef5c:	29132fe4 	stp	w4, w11, [sp, #152]
  4aef60:	b900a3e3 	str	w3, [sp, #160]
  4aef64:	a90a97e6 	stp	x6, x5, [sp, #168]
  4aef68:	f9005fec 	str	x12, [sp, #184]
  4aef6c:	97fffa6d 	bl	4ad920 <void std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >::_M_realloc_insert<Eigen::Matrix<float, 2, 1, 0, 2, 1> >(__gnu_cxx::__normal_iterator<Eigen::Matrix<float, 2, 1, 0, 2, 1>*, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > > >, Eigen::Matrix<float, 2, 1, 0, 2, 1>&&)>
  4aef70:	4f000404 	movi	v4.4s, #0x0
  4aef74:	b940a3e3 	ldr	w3, [sp, #160]
  4aef78:	29532fe4 	ldp	w4, w11, [sp, #152]
  4aef7c:	a94a97e6 	ldp	x6, x5, [sp, #168]
  4aef80:	f9405fec 	ldr	x12, [sp, #184]
  4aef84:	17fffd4f 	b	4ae4c0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x610>
  4aef88:	910383e2 	add	x2, sp, #0xe0
  4aef8c:	aa1a03e0 	mov	x0, x26
  4aef90:	29130fe4 	stp	w4, w3, [sp, #152]
  4aef94:	a90a17e6 	stp	x6, x5, [sp, #160]
  4aef98:	f9005bec 	str	x12, [sp, #176]
  4aef9c:	97fffa61 	bl	4ad920 <void std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >::_M_realloc_insert<Eigen::Matrix<float, 2, 1, 0, 2, 1> >(__gnu_cxx::__normal_iterator<Eigen::Matrix<float, 2, 1, 0, 2, 1>*, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > > >, Eigen::Matrix<float, 2, 1, 0, 2, 1>&&)>
  4aefa0:	29530fe4 	ldp	w4, w3, [sp, #152]
  4aefa4:	a94a17e6 	ldp	x6, x5, [sp, #160]
  4aefa8:	f9405bec 	ldr	x12, [sp, #176]
  4aefac:	17fffc83 	b	4ae1b8 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x308>
  4aefb0:	52800017 	mov	w23, #0x0                   	// #0
  4aefb4:	17fffd4b 	b	4ae4e0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x630>
  4aefb8:	52800037 	mov	w23, #0x1                   	// #1
  4aefbc:	17fffd49 	b	4ae4e0 <svo::feature_alignment::align2D(cv::Mat const&, unsigned char*, unsigned char*, int, bool, bool, Eigen::Matrix<double, 2, 1, 0, 2, 1>&, bool, std::vector<Eigen::Matrix<float, 2, 1, 0, 2, 1>, std::allocator<Eigen::Matrix<float, 2, 1, 0, 2, 1> > >*)+0x630>
