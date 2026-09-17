000000000049b180 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)>:
  49b180:	a9bc7bfd 	stp	x29, x30, [sp, #-64]!
  49b184:	1e6e1001 	fmov	d1, #1.000000000000000000e+00
  49b188:	52800027 	mov	w7, #0x1                   	// #1
  49b18c:	910003fd 	mov	x29, sp
  49b190:	6d401c15 	ldp	d21, d7, [x0]
  49b194:	1ac420e4 	lsl	w4, w7, w4
  49b198:	6d411814 	ldp	d20, d6, [x0, #16]
  49b19c:	1e220080 	scvtf	s0, w4
  49b1a0:	1e7408e2 	fmul	d2, d7, d20
  49b1a4:	1f668aa2 	fnmsub	d2, d21, d6, d2
  49b1a8:	1e621821 	fdiv	d1, d1, d2
  49b1ac:	1e6108c6 	fmul	d6, d6, d1
  49b1b0:	1e6188e7 	fnmul	d7, d7, d1
  49b1b4:	1e618a94 	fnmul	d20, d20, d1
  49b1b8:	1e610ab5 	fmul	d21, d21, d1
  49b1bc:	1e6240c6 	fcvt	s6, d6
  49b1c0:	1e6240e7 	fcvt	s7, d7
  49b1c4:	1e624294 	fcvt	s20, d20
  49b1c8:	1e6242b5 	fcvt	s21, d21
  49b1cc:	1e2008c6 	fmul	s6, s6, s0
  49b1d0:	1e2008e7 	fmul	s7, s7, s0
  49b1d4:	1e200a94 	fmul	s20, s20, s0
  49b1d8:	1e200ab5 	fmul	s21, s21, s0
  49b1dc:	1e2620c0 	fcmp	s6, s6
  49b1e0:	54002206 	b.vs	49b620 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x4a0>
  49b1e4:	f9400040 	ldr	x0, [x2]
  49b1e8:	1ac320e7 	lsl	w7, w7, w3
  49b1ec:	f9402422 	ldr	x2, [x1, #72]
  49b1f0:	1e2200e0 	scvtf	s0, w7
  49b1f4:	6d404811 	ldp	d17, d18, [x0]
  49b1f8:	f9400040 	ldr	x0, [x2]
  49b1fc:	1e624231 	fcvt	s17, d17
  49b200:	1e624252 	fcvt	s18, d18
  49b204:	b9400822 	ldr	w2, [x1, #8]
  49b208:	2a0003e9 	mov	w9, w0
  49b20c:	f9400823 	ldr	x3, [x1, #16]
  49b210:	7100005f 	cmp	w2, #0x0
  49b214:	1e201a31 	fdiv	s17, s17, s0
  49b218:	1e201a52 	fdiv	s18, s18, s0
  49b21c:	5400006d 	b.le	49b228 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0xa8>
  49b220:	710014bf 	cmp	w5, #0x5
  49b224:	54000900 	b.eq	49b344 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1c4>  // b.none
  49b228:	4b0503ea 	neg	w10, w5
  49b22c:	6b05015f 	cmp	w10, w5
  49b230:	5400086a 	b.ge	49b33c <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1bc>  // b.tcont
  49b234:	2a0a03eb 	mov	w11, w10
  49b238:	93407d28 	sxtw	x8, w9
  49b23c:	1e2e1005 	fmov	s5, #1.000000000000000000e+00
  49b240:	1e220170 	scvtf	s16, w11
  49b244:	2a0a03e3 	mov	w3, w10
  49b248:	1e340a13 	fmul	s19, s16, s20
  49b24c:	1e350a10 	fmul	s16, s16, s21
  49b250:	1400002c 	b	49b300 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x180>
  49b254:	b9400c24 	ldr	w4, [x1, #12]
  49b258:	11000447 	add	w7, w2, #0x1
  49b25c:	6b0400ff 	cmp	w7, w4
  49b260:	5400062a 	b.ge	49b324 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1a4>  // b.tcont
  49b264:	b9400824 	ldr	w4, [x1, #8]
  49b268:	11000407 	add	w7, w0, #0x1
  49b26c:	6b0400ff 	cmp	w7, w4
  49b270:	540005aa 	b.ge	49b324 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1a4>  // b.tcont
  49b274:	1e220003 	scvtf	s3, w0
  49b278:	1e220042 	scvtf	s2, w2
  49b27c:	1b007d20 	mul	w0, w9, w0
  49b280:	93407c42 	sxtw	x2, w2
  49b284:	f9400824 	ldr	x4, [x1, #16]
  49b288:	11000463 	add	w3, w3, #0x1
  49b28c:	1e223822 	fsub	s2, s1, s2
  49b290:	1e233801 	fsub	s1, s0, s3
  49b294:	8b20c040 	add	x0, x2, w0, sxtw
  49b298:	6b05007f 	cmp	w3, w5
  49b29c:	8b000082 	add	x2, x4, x0
  49b2a0:	8b080047 	add	x7, x2, x8
  49b2a4:	1e2138a3 	fsub	s3, s5, s1
  49b2a8:	1e2238a4 	fsub	s4, s5, s2
  49b2ac:	3c686840 	ldr	b0, [x2, x8]
  49b2b0:	3c606897 	ldr	b23, [x4, x0]
  49b2b4:	3d400456 	ldr	b22, [x2, #1]
  49b2b8:	1e230898 	fmul	s24, s4, s3
  49b2bc:	5e21d800 	scvtf	s0, s0
  49b2c0:	1e210884 	fmul	s4, s4, s1
  49b2c4:	5e21daf7 	scvtf	s23, s23
  49b2c8:	1e220862 	fmul	s2, s3, s2
  49b2cc:	5e21dad6 	scvtf	s22, s22
  49b2d0:	3d4004e3 	ldr	b3, [x7, #1]
  49b2d4:	1e3838a1 	fsub	s1, s5, s24
  49b2d8:	1e240800 	fmul	s0, s0, s4
  49b2dc:	1f1802e0 	fmadd	s0, s23, s24, s0
  49b2e0:	5e21d863 	scvtf	s3, s3
  49b2e4:	1e243821 	fsub	s1, s1, s4
  49b2e8:	1f0202c0 	fmadd	s0, s22, s2, s0
  49b2ec:	1e223821 	fsub	s1, s1, s2
  49b2f0:	1f010060 	fmadd	s0, s3, s1, s0
  49b2f4:	7ea1b800 	fcvtzu	s0, s0
  49b2f8:	3c0014c0 	str	b0, [x6], #1
  49b2fc:	540001aa 	b.ge	49b330 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1b0>  // b.tcont
  49b300:	1e220060 	scvtf	s0, w3
  49b304:	1f064c01 	fmadd	s1, s0, s6, s19
  49b308:	1f074000 	fmadd	s0, s0, s7, s16
  49b30c:	1e212a21 	fadd	s1, s17, s1
  49b310:	1e202a40 	fadd	s0, s18, s0
  49b314:	1e300022 	fcvtms	w2, s1
  49b318:	1e300000 	fcvtms	w0, s0
  49b31c:	2a000044 	orr	w4, w2, w0
  49b320:	36fff9a4 	tbz	w4, #31, 49b254 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0xd4>
  49b324:	52800000 	mov	w0, #0x0                   	// #0
  49b328:	a8c47bfd 	ldp	x29, x30, [sp], #64
  49b32c:	d65f03c0 	ret
  49b330:	1100056b 	add	w11, w11, #0x1
  49b334:	6b05017f 	cmp	w11, w5
  49b338:	54fff84b 	b.lt	49b240 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0xc0>  // b.tstop
  49b33c:	52800020 	mov	w0, #0x1                   	// #1
  49b340:	17fffffa 	b	49b328 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1a8>
  49b344:	b9400c24 	ldr	w4, [x1, #12]
  49b348:	7100009f 	cmp	w4, #0x0
  49b34c:	5400156d 	b.le	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>
  49b350:	51000447 	sub	w7, w2, #0x1
  49b354:	8b24c06a 	add	x10, x3, w4, sxtw
  49b358:	910190c8 	add	x8, x6, #0x64
  49b35c:	93407ce7 	sxtw	x7, w7
  49b360:	9b0028e0 	madd	x0, x7, x0, x10
  49b364:	eb0000df 	cmp	x6, x0
  49b368:	1a9f37e0 	cset	w0, cs  // cs = hs, nlast
  49b36c:	eb03011f 	cmp	x8, x3
  49b370:	1a9f87e3 	cset	w3, ls  // ls = plast
  49b374:	2a030000 	orr	w0, w0, w3
  49b378:	34001400 	cbz	w0, 49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>
  49b37c:	1e2638c0 	fsub	s0, s6, s6
  49b380:	1e202000 	fcmp	s0, s0
  49b384:	540013a1 	b.ne	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>  // b.any
  49b388:	1e2738e0 	fsub	s0, s7, s7
  49b38c:	1e202000 	fcmp	s0, s0
  49b390:	54001341 	b.ne	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>  // b.any
  49b394:	1e343a80 	fsub	s0, s20, s20
  49b398:	1e202000 	fcmp	s0, s0
  49b39c:	540012e1 	b.ne	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>  // b.any
  49b3a0:	1e353aa0 	fsub	s0, s21, s21
  49b3a4:	1e202000 	fcmp	s0, s0
  49b3a8:	54001281 	b.ne	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>  // b.any
  49b3ac:	1e313a20 	fsub	s0, s17, s17
  49b3b0:	1e202000 	fcmp	s0, s0
  49b3b4:	54001221 	b.ne	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>  // b.any
  49b3b8:	1e323a40 	fsub	s0, s18, s18
  49b3bc:	1e202000 	fcmp	s0, s0
  49b3c0:	540011c1 	b.ne	49b5f8 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x478>  // b.any
  49b3c4:	f0001ea3 	adrp	x3, 872000 <typeinfo for std::_Sp_counted_ptr<svo::AllPixelsDetector*, (__gnu_cxx::_Lock_policy)2>>
  49b3c8:	3dc1bc60 	ldr	q0, [x3, #1776]
  49b3cc:	2f000419 	mvni	v25.2s, #0x0
  49b3d0:	4e0404d8 	dup	v24.4s, v6.s[0]
  49b3d4:	aa0603ec 	mov	x12, x6
  49b3d8:	4e040637 	dup	v23.4s, v17.s[0]
  49b3dc:	93407d2b 	sxtw	x11, w9
  49b3e0:	4e0404f6 	dup	v22.4s, v7.s[0]
  49b3e4:	f9000bf3 	str	x19, [sp, #16]
  49b3e8:	4e040653 	dup	v19.4s, v18.s[0]
  49b3ec:	9100c3f3 	add	x19, sp, #0x30
  49b3f0:	4f0707f0 	movi	v16.4s, #0xff
  49b3f4:	1280008d 	mov	w13, #0xfffffffb            	// #-5
  49b3f8:	4f03f604 	fmov	v4.4s, #1.000000000000000000e+00
  49b3fc:	5280008f 	mov	w15, #0x4                   	// #4
  49b400:	5280004e 	mov	w14, #0x2                   	// #2
  49b404:	3d800fe0 	str	q0, [sp, #48]
  49b408:	1e2201a0 	scvtf	s0, w13
  49b40c:	1280008a 	mov	w10, #0xfffffffb            	// #-5
  49b410:	52800088 	mov	w8, #0x4                   	// #4
  49b414:	1e340805 	fmul	s5, s0, s20
  49b418:	1e350800 	fmul	s0, s0, s21
  49b41c:	4e0404a5 	dup	v5.4s, v5.s[0]
  49b420:	4e040403 	dup	v3.4s, v0.s[0]
  49b424:	11001543 	add	w3, w10, #0x5
  49b428:	51000484 	sub	w4, w4, #0x1
  49b42c:	3dc00260 	ldr	q0, [x19]
  49b430:	1e220062 	scvtf	s2, w3
  49b434:	4ea51cbc 	mov	v28.16b, v5.16b
  49b438:	51000442 	sub	w2, w2, #0x1
  49b43c:	4e040c9b 	dup	v27.4s, w4
  49b440:	7100091f 	cmp	w8, #0x2
  49b444:	4e040c41 	dup	v1.4s, w2
  49b448:	4e040442 	dup	v2.4s, v2.s[0]
  49b44c:	4e20d442 	fadd	v2.4s, v2.4s, v0.4s
  49b450:	4ea31c60 	mov	v0.16b, v3.16b
  49b454:	4e38cc5c 	fmla	v28.4s, v2.4s, v24.4s
  49b458:	4e36cc40 	fmla	v0.4s, v2.4s, v22.4s
  49b45c:	4e3cd6fc 	fadd	v28.4s, v23.4s, v28.4s
  49b460:	4e20d660 	fadd	v0.4s, v19.4s, v0.4s
  49b464:	4e21bb9a 	fcvtms	v26.4s, v28.4s
  49b468:	4e21b81d 	fcvtms	v29.4s, v0.4s
  49b46c:	6ea08b42 	cmge	v2.4s, v26.4s, #0
  49b470:	6ea08bbe 	cmge	v30.4s, v29.4s, #0
  49b474:	4eba377b 	cmgt	v27.4s, v27.4s, v26.4s
  49b478:	4ebd3421 	cmgt	v1.4s, v1.4s, v29.4s
  49b47c:	4e3e1c42 	and	v2.16b, v2.16b, v30.16b
  49b480:	4e3b1c42 	and	v2.16b, v2.16b, v27.16b
  49b484:	4e211c41 	and	v1.16b, v2.16b, v1.16b
  49b488:	54000061 	b.ne	49b494 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x314>  // b.any
  49b48c:	6e140721 	mov	v1.s[2], v25.s[0]
  49b490:	6e1c0721 	mov	v1.s[3], v25.s[0]
  49b494:	6eb1a821 	uminv	s1, v1.4s
  49b498:	1e260022 	fmov	w2, s1
  49b49c:	34000ac2 	cbz	w2, 49b5f4 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x474>
  49b4a0:	0e0c3fa2 	mov	w2, v29.s[1]
  49b4a4:	1e2603a3 	fmov	w3, s29
  49b4a8:	0e0c3f44 	mov	w4, v26.s[1]
  49b4ac:	1e260347 	fmov	w7, s26
  49b4b0:	f9400830 	ldr	x16, [x1, #16]
  49b4b4:	7100111f 	cmp	w8, #0x4
  49b4b8:	1b097c63 	mul	w3, w3, w9
  49b4bc:	1b097c42 	mul	w2, w2, w9
  49b4c0:	93407ce7 	sxtw	x7, w7
  49b4c4:	93407c84 	sxtw	x4, w4
  49b4c8:	8b23c0e3 	add	x3, x7, w3, sxtw
  49b4cc:	8b22c082 	add	x2, x4, w2, sxtw
  49b4d0:	8b030211 	add	x17, x16, x3
  49b4d4:	8b020202 	add	x2, x16, x2
  49b4d8:	aa1103e7 	mov	x7, x17
  49b4dc:	aa0203e4 	mov	x4, x2
  49b4e0:	540001a1 	b.ne	49b514 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x394>  // b.any
  49b4e4:	0e143fa7 	mov	w7, v29.s[2]
  49b4e8:	0e1c3fa4 	mov	w4, v29.s[3]
  49b4ec:	0e143f5e 	mov	w30, v26.s[2]
  49b4f0:	0e1c3f52 	mov	w18, v26.s[3]
  49b4f4:	1b097ce7 	mul	w7, w7, w9
  49b4f8:	1b097c84 	mul	w4, w4, w9
  49b4fc:	93407fde 	sxtw	x30, w30
  49b500:	93407e52 	sxtw	x18, w18
  49b504:	8b27c3c7 	add	x7, x30, w7, sxtw
  49b508:	8b24c244 	add	x4, x18, w4, sxtw
  49b50c:	8b070207 	add	x7, x16, x7
  49b510:	8b040204 	add	x4, x16, x4
  49b514:	7c6b6a21 	ldr	h1, [x17, x11]
  49b518:	7100111f 	cmp	w8, #0x4
  49b51c:	7c6b685f 	ldr	h31, [x2, x11]
  49b520:	7c636a02 	ldr	h2, [x16, x3]
  49b524:	0e020421 	dup	v1.4h, v1.h[0]
  49b528:	7c6b68fb 	ldr	h27, [x7, x11]
  49b52c:	4e21dbbd 	scvtf	v29.4s, v29.4s
  49b530:	7c6b689e 	ldr	h30, [x4, x11]
  49b534:	0e020442 	dup	v2.4h, v2.h[0]
  49b538:	4e21db5a 	scvtf	v26.4s, v26.4s
  49b53c:	6e0607e1 	mov	v1.h[1], v31.h[0]
  49b540:	4ebdd400 	fsub	v0.4s, v0.4s, v29.4s
  49b544:	0d404842 	ld1	{v2.h}[1], [x2]
  49b548:	4ebad79a 	fsub	v26.4s, v28.4s, v26.4s
  49b54c:	6e0a0761 	mov	v1.h[2], v27.h[0]
  49b550:	4ea0d49b 	fsub	v27.4s, v4.4s, v0.4s
  49b554:	0d4050e2 	ld1	{v2.h}[2], [x7]
  49b558:	4ebad49c 	fsub	v28.4s, v4.4s, v26.4s
  49b55c:	6e0e07c1 	mov	v1.h[3], v30.h[0]
  49b560:	0d405882 	ld1	{v2.h}[3], [x4]
  49b564:	6e20df80 	fmul	v0.4s, v28.4s, v0.4s
  49b568:	2f10a421 	uxtl	v1.4s, v1.4h
  49b56c:	6e3cdf7c 	fmul	v28.4s, v27.4s, v28.4s
  49b570:	2f10a442 	uxtl	v2.4s, v2.4h
  49b574:	6e3adf7b 	fmul	v27.4s, v27.4s, v26.4s
  49b578:	4e301c3e 	and	v30.16b, v1.16b, v16.16b
  49b57c:	4ebcd49a 	fsub	v26.4s, v4.4s, v28.4s
  49b580:	4e301c5d 	and	v29.16b, v2.16b, v16.16b
  49b584:	6f380442 	ushr	v2.4s, v2.4s, #8
  49b588:	6e21dbde 	ucvtf	v30.4s, v30.4s
  49b58c:	4ea0d75a 	fsub	v26.4s, v26.4s, v0.4s
  49b590:	6e21dbbd 	ucvtf	v29.4s, v29.4s
  49b594:	6f380421 	ushr	v1.4s, v1.4s, #8
  49b598:	6e3edc00 	fmul	v0.4s, v0.4s, v30.4s
  49b59c:	6e21d842 	ucvtf	v2.4s, v2.4s
  49b5a0:	4ebbd75a 	fsub	v26.4s, v26.4s, v27.4s
  49b5a4:	6e21d821 	ucvtf	v1.4s, v1.4s
  49b5a8:	4e3dcf80 	fmla	v0.4s, v28.4s, v29.4s
  49b5ac:	4e22cf60 	fmla	v0.4s, v27.4s, v2.4s
  49b5b0:	4e21cf40 	fmla	v0.4s, v26.4s, v1.4s
  49b5b4:	6ea1b800 	fcvtzu	v0.4s, v0.4s
  49b5b8:	0e612801 	xtn	v1.4h, v0.4s
  49b5bc:	4e612801 	xtn2	v1.8h, v0.4s
  49b5c0:	0e212820 	xtn	v0.8b, v1.8h
  49b5c4:	fd0017e0 	str	d0, [sp, #40]
  49b5c8:	54000260 	b.eq	49b614 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x494>  // b.none
  49b5cc:	794053e2 	ldrh	w2, [sp, #40]
  49b5d0:	79000182 	strh	w2, [x12]
  49b5d4:	1100114a 	add	w10, w10, #0x4
  49b5d8:	8b28418c 	add	x12, x12, w8, uxtw
  49b5dc:	71001d5f 	cmp	w10, #0x7
  49b5e0:	54000100 	b.eq	49b600 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x480>  // b.none
  49b5e4:	71000d5f 	cmp	w10, #0x3
  49b5e8:	29411022 	ldp	w2, w4, [x1, #8]
  49b5ec:	1a8f01c8 	csel	w8, w14, w15, eq  // eq = none
  49b5f0:	17ffff8d 	b	49b424 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x2a4>
  49b5f4:	f9400bf3 	ldr	x19, [sp, #16]
  49b5f8:	1280008a 	mov	w10, #0xfffffffb            	// #-5
  49b5fc:	17ffff0e 	b	49b234 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0xb4>
  49b600:	110005ad 	add	w13, w13, #0x1
  49b604:	710015bf 	cmp	w13, #0x5
  49b608:	54000320 	b.eq	49b66c <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x4ec>  // b.none
  49b60c:	29411022 	ldp	w2, w4, [x1, #8]
  49b610:	17ffff7e 	b	49b408 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x288>
  49b614:	b9402be2 	ldr	w2, [sp, #40]
  49b618:	b9000182 	str	w2, [x12]
  49b61c:	17ffffee 	b	49b5d4 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x454>
  49b620:	2a0703e3 	mov	w3, w7
  49b624:	f0001ea1 	adrp	x1, 872000 <typeinfo for std::_Sp_counted_ptr<svo::AllPixelsDetector*, (__gnu_cxx::_Lock_policy)2>>
  49b628:	9118c021 	add	x1, x1, #0x630
  49b62c:	52801022 	mov	w2, #0x81                  	// #129
  49b630:	f9000bf3 	str	x19, [sp, #16]
  49b634:	9100c3f3 	add	x19, sp, #0x30
  49b638:	aa1303e0 	mov	x0, x19
  49b63c:	940b3c51 	bl	76a780 <google::LogMessage::LogMessage(char const*, int, int)>
  49b640:	aa1303e0 	mov	x0, x19
  49b644:	940b3aab 	bl	76a0f0 <google::LogMessage::stream()>
  49b648:	f0001ea1 	adrp	x1, 872000 <typeinfo for std::_Sp_counted_ptr<svo::AllPixelsDetector*, (__gnu_cxx::_Lock_policy)2>>
  49b64c:	d28006c2 	mov	x2, #0x36                  	// #54
  49b650:	9119c021 	add	x1, x1, #0x670
  49b654:	940c7857 	bl	7b97b0 <std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long)>
  49b658:	aa1303e0 	mov	x0, x19
  49b65c:	940b3d11 	bl	76aaa0 <google::LogMessage::~LogMessage()>
  49b660:	52800000 	mov	w0, #0x0                   	// #0
  49b664:	f9400bf3 	ldr	x19, [sp, #16]
  49b668:	17ffff30 	b	49b328 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1a8>
  49b66c:	f9400bf3 	ldr	x19, [sp, #16]
  49b670:	17ffff2e 	b	49b328 <svo::warp::warpAffine(Eigen::Matrix<double, 2, 2, 0, 2, 2> const&, cv::Mat const&, Eigen::Ref<Eigen::Matrix<double, 2, 1, 0, 2, 1>, 0, Eigen::InnerStride<1> > const&, int, int, int, unsigned char*)+0x1a8>
  49b674:	aa0003e1 	mov	x1, x0
  49b678:	aa1303e0 	mov	x0, x19
  49b67c:	aa0103f3 	mov	x19, x1
  49b680:	940b3d08 	bl	76aaa0 <google::LogMessage::~LogMessage()>
  49b684:	aa1303e0 	mov	x0, x19
  49b688:	940d353e 	bl	7e8b80 <_Unwind_Resume>
  49b68c:	d503201f 	nop
