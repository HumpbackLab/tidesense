# OV9281 DataSheet

> 来源：`OV9281 DataSheet.pdf`
> 本文件由 PDF 文本提取生成；每页以 `Page` 标题分隔。

## Page 1

        Confidential      Omnivision            for           only



                                          datasheet
                                                 PRELIMINARY SPECIFICATION                                                                                                                                 OV9281
                                     1/4" b&w CMOS 1 megapixel (1280 x 800) image sensor
                                                          with OmniPixel3-GS™ technology





Powered by TCPDF (www.tcpdf.org)

---

## Page 2

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                 00Copyright ©2016 OmniVision Technologies, Inc. All rights reserved.

                                 This document is provided “as is” with no warranties whatsoever, including any warranty of merchantability,
                                  non-infringement, fitness for any particular purpose, or any warranty otherwise arising out of any proposal, specification,
                                   or sample.

                               OmniVision Technologies, Inc. and all its affiliates disclaim all liability, including liability for infringement of any proprietary
                                         rights, relating to the use of information in this document. No license, expressed or implied, by estoppel or otherwise, to
                             any intellectual property rights is granted herein.

                            The information contained in this document is considered proprietary to OmniVision Technologies, Inc. and all its
                                            affiliates. This information may be distributed to individuals or organizations authorized by OmniVision Technologies, Inc.
                                      to receive said information. Individuals and/or organizations are not allowed to re-distribute said information.




                          Trademark Information

                               OmniVision and the OmniVision logo are registered trademarks of OmniVision Technologies, Inc. OmniPixel3-GS is a
                               trademark of OmniVision Technologies, Inc.

                                           All other trademarks used herein are the property of their respective owners.        Confidential      Omnivision                    b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™fortechnology

                                 datasheet (CSP5)
                          PRELIMINARY SPECIFICATION           only
                                   version 1.1
                         may 2016



                              To learn more about OmniVision Technologies, visit www.ovt.com.
                               OmniVision Technologies is publicly traded on NASDAQ under the symbol OVTI.





Powered by TCPDF (www.tcpdf.org)

---

## Page 3

                                                                                                                                                                                                                                i


00applications                    ordering information

          industrial bar code scanning                             OV09281-H64A (b&w, lead-free)
                                                                        64-pin CSP5



00features

       3 µm x 3 µm pixel with OmniPixel3-GS™ technology             supports horizontal and vertical 2:1 and 4:1
                                                         monochrome subsampling
        automatic black level calibration (ABLC)
                                                                      support for image sizes: 1280x800, 1280x720,
       programmable controls for frame rate, mirror and flip,
                                                                 640x480, 640x400
        cropping and windowing
                                                        embedded 256 bits of one-time programmable (OTP)
        support output formats: 8/10-bit RAW
                                                        memory for part identification
          fast mode switching
                                                                two on-chip phase lock loops (PLLs)
        supports 2x2 monochrome binning
                                                   LED PWM
        two-lane MIPI serial output interface
                                                                                           built-in strobe control
      DVP parallel output interface


00key specifications (typical)

        active array size: 1296 x 816                                 input clock frequency: 6~27 MHz

       power supply:                                             lens chief ray angle: 9° linear
          analog: 2.8V (nominal)                            max S/N ratio: TBD
           core: 1.2V (nominal)         Confidentialdynamic range: TBD                     note
            I/O: 1.8V (nominal)                                                                          Maximum integration
                                                  maximum image transfer rate:                        time of dark current       power requirements: Omnivision
                                                              1280 x 800: 120 fps (see table 2-1)                 depends on read out
            active: 134 mW                                                                                                             speed (e.g., for 100fps,
                                                                             sensitivity: TBD          standby: 65 µA                                                                           max dark current is       XSHUTDOWN: 50 µA                                 scan mode: progressive for around 3.5e- at 50°C).

       temperature range:                                minimum exposure time: 1 row period
           operating: -30°C to +85°C junction temperature                                                  maximum exposure time: frame length - 12 row           only
           stable image: 0°C to 50°C junction temperature                 periods, where frame length is set by registers
                                                                   {0x380E, 0x380F}       output interface: 2-lane MIPI serial output and DVP
          parallel output                                                   pixel size: 3 µm x 3 µm
       output formats: 10-bit RAW                                dark current: TBD
        lens size: 1/4"                                        image area: 3896 µm x 2453 µm

                                                           package dimensions: 5237 µm x 4463 µm





       05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





                   Powered by TCPDF (www.tcpdf.org)

---

## Page 4

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology

        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies            PRELIMINARY SPECIFICATION                  version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 5

                                                                                                                                                                                                                          iii


 00table of contents


        1 signal descriptions                                                                 1-1
        2 system level description                                                            2-1
             2.1 overview                                                                     2-1
             2.2 architecture                                                                  2-1
             2.3 format and frame                                                             2-4
                  2.3.1 MIPI interface                                                           2-5
                  2.3.2 VSYNC timing in MIPI mode                                               2-6
             2.4 I/O control                                                                   2-7
             2.5 power management                                                            2-8
                  2.5.1 power up sequence                                                       2-8
                  2.5.2 power down sequence                                                 2-10
             2.6 reset                                                                    2-13
                  2.6.1 power ON reset generation                                             2-13
             2.7 hardware and software standby                                               2-13
             2.8 system clock control                                                        2-14
                  2.8.1 PLL configuration                                                     2-14
             2.9 serial camera control bus (SCCB) interface                                      2-18
                  2.9.1 data transfer protocol                                                 2-18
                  2.9.2 message format                                                      2-18
                  2.9.3 read / write operation                                                 2-19
                  2.9.4 SCCB timing                                                         2-22        Confidential
                  2.9.5 group write and fast mode switching                                      2-23      Omnivision        3 block level description                                                              3-1
             3.1 pixel array structure                                                           3-1             3.2 subsampling        for3-2
        4 image sensor core digital functions                                                   4-1
             4.1 mirror and flip                                                                4-1           only
             4.2 image windowing                                                              4-2
             4.3 test pattern                                                                  4-3
                  4.3.1 general test pattern bar                                                   4-3
                  4.3.2 solid color test pattern                                                   4-3
             4.4 black level calibration (BLC)                                                     4-4





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 6

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                                    4.5 one time programmable (OTP) memory                                            4-7
                                           4.5.1 OTP memory structure                                                    4-7
                                           4.5.2 accessing the OTP memory                                                4-7
                                           4.5.3 procedure for accessing OTP memory                                       4-9
                                           4.5.4 procedure to read OTP content                                             4-9
                                           4.5.5 procedure to program OTP content                                         4-9
                                           4.5.6 power supply requirement for OTP memory programming                     4-10
                                    4.6 LED PWM                                                                4-10
                                    4.7 strobe                                                                   4-11
                                    4.8 low power modes                                                          4-12
                                           4.8.1 low frame rate mode                                                  4-13
                                           4.8.2 snapshot mode                                                       4-13
                                           4.8.3 external trigger snapshot mode                                         4-14
                          5 image sensor processor digital functions                                              5-1
                                    5.1 ISP general controls                                                           5-1
                                    5.2 manual white balance (MWB)                                                    5-1
                                    5.3 manual exposure and gain control                                                5-2
                          6 system control                                                                    6-1
                                    6.1 mobile industry processor interface (MIPI)                                        6-1
                                    6.2 digital video port (DVP)                                                        6-8
                          7 register tables                                                                    7-1
                                    7.1 system control [0x0100 ~ 0x010A, 0x3000 ~ 0x301F ~ 0x303F]                       7-1
                                    7.2 PLL control [0x0300 ~ 0x0319]                                                  7-8                                    7.3ConfidentialSCCB and group hold control [0x3100 ~ 0x3107, 0x31FF ~ 0x320F]                  7-10      Omnivision7.4 manual AWB_gain control [0x3400 ~ 0x3406]                                    7-12
                                    7.5 manual AEC/AGC [0x3500 ~ 0x3512, 0x3519 ~ 0x351D]                           7-12
                                    7.6 analog control [0x3600 ~ 0x3684]                                             7-14            for
                                    7.7 sensor control [0x3700 ~ 0x37AF]                                             7-15
                                    7.8 timing control [0x3800 ~ 0x3835, 0x3837]                                      7-15           only
                                    7.9 PWM and strobe control [0x3900 ~ 0x3904, 0x3910 ~ 0x391D, 0x3920 ~ 0x3933]       7-18
                                  7.10 low power mode control [0x4F00 ~ 0x4F0D, 0x4F10 ~ 0x4F14]                     7-20
                                  7.11 BIST [0x3E00 ~ 0x3E12]                                                    7-21
                                  7.12 OTP control [0x3D80 ~ 0x3D87]                                              7-23
                                  7.13 BLC control [0x4000 ~ 0x4017, 0x4020 ~ 0x403F, 0x4042 ~ 0x4049]                7-24
                                  7.14 frame control [0x4240 ~ 0x4244]                                             7-27





                                 proprietary to OmniVision Technologies            PRELIMINARY SPECIFICATION                  version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 7

                                                                                                                                   v




            7.15 format control [0x4300 ~ 0x4307, 0x4311 ~ 0x4317, 0x4320, 0x4322 ~ 0x4329]      7-28
            7.16 VFIFO control [0x4600 ~ 0x4602]                                            7-30
            7.17 DVP control [0x4701 ~ 0x4709, 0x470C, 0x470F]                               7-30
            7.18 MIPI top [0x4800 ~ 0x4808, 0x4810 ~ 0x483D, 0x484A~ 0x484F]                   7-32
            7.19 ISP top [0x5000 ~ 0x5018, 0x5020 ~ 0x5024, 0x5030 ~ 0x5035, 0x5E00 ~ 0x5E2E]    7-39
            7.20 window control [0x5A00 ~ 0x5A09, 0x5A10 ~ 0x5A2F]                            7-44
        8 operating specifications                                                            8-1
             8.1 absolute maximum ratings                                                      8-1
             8.2 functional temperature                                                         8-1
             8.3 DC characteristics                                                             8-2
             8.4 timing characteristics                                                          8-3
        9 mechanical specifications                                                           9-1
             9.1 physical specifications                                                         9-1
             9.2 IR reflow specifications                                                        9-2
        10 optical specifications                                                          10-1
            10.1 sensor array center                                                        10-1
            10.2 lens chief ray angle (CRA)                                                   10-2
            10.3 IR cut off wavelength                                                      10-3        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 8

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology

        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies            PRELIMINARY SPECIFICATION                  version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 9

                                                                                                                                                                                 vii


 00list of figures


         figure 1-1    pin diagram                                                               1-5
         figure 2-1   OV9281 block diagram                                                     2-2
         figure 2-2   OV9281 MIPI reference design schematic                                      2-3
         figure 2-3   OV9281 DVP reference design schematic                                      2-4
         figure 2-4   MIPI timing                                                               2-5
         figure 2-5   VSYNC timing in mode 1                                                    2-6
         figure 2-6   VSYNC timing in mode 2                                                    2-6
         figure 2-7   VSYNC timing in mode 3                                                    2-6
         figure 2-8   power up sequence                                                         2-9
         figure 2-9   power down sequence                                                   2-11
         figure 2-10  standby sequence                                                      2-12
         figure 2-11  PLL1 clock diagram                                                     2-14
         figure 2-12  PLL2 clock diagram                                                     2-14
         figure 2-13  clock connection diagram                                                 2-17
         figure 2-14  message type                                                          2-18
         figure 2-15  SCCB single read from random location                                     2-19
         figure 2-16  SCCB single read from current location                                     2-19
         figure 2-17  SCCB sequential read from random location                                 2-20
         figure 2-18  SCCB sequential read from current location                                  2-20
         figure 2-19  SCCB single write to random location                                       2-21        Confidential         figure 2-20  SCCB sequential write to random location                                   2-21         figure 2-21  SCCB interfaceOmnivisiontiming                                                   2-22
         figure 3-1   sensor array region color filter layout                                         3-1
         figure 3-2   example of 2x2 binning                                                     3-2            for
         figure 3-3   example of 2:1 skipping                                                     3-2
         figure 3-4   example of 4:1 skipping                                                     3-2           only
         figure 4-1    mirror and flip samples                                                     4-1
         figure 4-2   image windowing                                                          4-2
         figure 4-3    test pattern                                                               4-3
         figure 4-4   OTP access                                                               4-9
         figure 4-5   LED PWM output timing                                                  4-10
         figure 4-6   low frame rate mode timing                                               4-13





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 10

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                                 figure 4-7   snapshot mode timing                                                   4-13
                                 figure 4-8    external snapshot mode timing                                            4-14
                                 figure 4-9   frame triggered by red pulse diagram                                       4-14
                                 figure 9-1   package specifications                                                     9-1
                                 figure 9-2    IR reflow ramp rate requirements                                             9-2
                                 figure 10-1  sensor array center                                                     10-1
                                 figure 10-2   chief ray angle (CRA)                                                    10-2
        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies            PRELIMINARY SPECIFICATION                  version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 11

                                                                                                                                                            ix


 00list of tables


        table 1-1  signal descriptions                                                           1-1
        table 1-2  configuration under various conditions                                          1-3
        table 1-3  pad symbol and equivalent circuit                                               1-5
        table 2-1  supported resolution and frame rate                                            2-4
        table 2-2  MIPI timing specifications (2-lane mode)                                         2-5
        table 2-3  I/O control registers                                                          2-7
        table 2-4  power up sequence                                                           2-8
        table 2-5  power up sequence timing constraints                                           2-8
        table 2-6  power down sequence                                                     2-10
        table 2-7  power down sequence timing constraints                                      2-10
        table 2-8  hardware and software standby description                                    2-13
        table 2-9  PLL control registers                                                      2-14
        table 2-10  sample PLL configuration                                                 2-16
        table 2-11  PLL speed limitation                                                     2-17
        table 2-12  SCCB interface timing specifications                                        2-22
        table 2-13  context switching control                                                 2-23
        table 3-1  binning-related registers                                                      3-2
        table 4-1  mirror and flip registers                                                       4-1
        table 4-2  image windowing control functions                                              4-2
        table 4-3  general test pattern bar selection control                                        4-3        Confidential        table 4-4  solid color test pattern control                                                 4-3        table 4-5  BLC controlOmnivisionfunctions                                                         4-4
        table 4-6  ALS algorithm control registers                                                4-5
        table 4-7  OTP memory structure                                                        4-7            for
        table 4-8  LED PWM registers                                                       4-11
        table 4-9  non-volatile memory map table (OTP)                                         4-11           only
        table 4-10  strobe control registers                                                   4-11
        table 4-11  low power mode control registers                                           4-12
        table 5-1  ISP top registers                                                             5-1
        table 5-2  manual AWB_gain registers                                                    5-1
        table 5-3  manual exposure and gain control registers                                       5-2
        table 6-1  MIPI top control registers                                                     6-1





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 12

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                                table 6-2  DVP control registers                                                         6-8
                                table 7-1  system control registers                                                      7-1
                                table 7-2  PLL control registers                                                         7-8
                                table 7-3  SCCB and group hold registers                                              7-10
                                table 7-4  manual AWB_gain registers                                                 7-12
                                table 7-5  manual AEC/AGC registers                                                 7-12
                                table 7-6  analog control registers                                                    7-14
                                table 7-7  sensor control registers                                                    7-15
                                table 7-8  timing control registers                                                    7-15
                                table 7-9 PWM and strobe control registers                                            7-18
                                table 7-10  low power mode control registers                                           7-20
                                table 7-11  BIST control registers                                                    7-21
                                table 7-12  OTP control registers                                                    7-23
                                table 7-13  BLC control registers                                                     7-24
                                table 7-14  frame control registers                                                   7-27
                                table 7-15  format control registers                                                  7-28
                                table 7-16  VFIFO control registers                                                   7-30
                                table 7-17  DVP control registers                                                    7-30
                                table 7-18  MIPI top control registers                                                 7-32
                                table 7-19  ISP top registers                                                        7-39
                                table 7-20  window control registers                                                  7-44
                                table 8-1  absolute maximum ratings                                                     8-1
                                table 8-2  functional temperature                                                       8-1        Confidential
                                table 8-3  DC characteristics (TA = 23°C ± 2°C)                                             8-2      Omnivision
                                table 8-4  timing characteristics                                                        8-3
                                table 9-1  package dimensions                                                          9-1            for                                table 9-2  reflow conditions                                                            9-2
                                table 10-1  CRA versus image height plot                                              10-2           only





                                 proprietary to OmniVision Technologies            PRELIMINARY SPECIFICATION                  version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 13

                                                                                                                                    1-1


  1  signal descriptions


        table 1-1 lists the signal descriptions and their corresponding pin numbers for the OV9281 image sensor. The package
        information is shown in section 9.

      table 1-1      signal descriptions (sheet 1 of 3)

         pin                                pin
       number    signal name          type          description

       A1       NC                  –            no connect

       A2       DVDD                power        power for digital circuit

       A3      DOGND               ground       ground for I/O

       A4       SDA                     I/O        SCCB data

                                                                 reset and power down (active low with internal pull down
       A5       XSHUTDOWN           input
                                                                        resistor)

       A6       ILPWM                  I/O           mechanical shutter output indicator

       A7      DOGND               ground       ground for I/O

       A8       NC                  –            no connect

       B1       DVDD                power        power for digital circuit

       B2      DOGND               ground       ground for I/O

       B3       DVDD                power        power for digital circuit

       B4       D8                       I/O        DVP output data[8]

       B5        FSIN/VSYNC            I/O           frame sync input / vertical sync output

       B6       STROBE                I/O           frame exposure output indicator        Confidential
       B7       DOVDD              power        power for I/O circuit      Omnivision
       B8       AVDD                power        power for analog circuit

       C1       AVDD                power        power for analog circuit            for
       C2       PCLK                   I/O        DVP output clock

       C3        D1/GPIO1/ROI1         I/O        DVP output data[1] / general purpose I/O / ROI1           only
       C4       D9                       I/O        DVP output data[9]

       C5       DVDD                power        power for digital circuit

       C6       ULPM                   I/O         ALS trigger indicator

       C7       ATEST                 analog        analog test

       C8       AGND                ground       ground for analog circuit

       D1       HREF                   I/O        DVP output horizontal reference





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 14

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 1-1      signal descriptions (sheet 2 of 3)

                                 pin                                pin
                          number    signal name          type          description

                          D2      AGND                ground       ground for analog circuit

                          D3       DOVDD              power        power for I/O circuit

                          D4        D0/GPIO0/ROI0         I/O        DVP output data[0] / general purpose I/O / ROI0

                          D5       PVDD                power       PLL analog power

                          D6        GPIO2/CONTEXT       I/O            general purpose I/O / context switch select

                          D7       VN1                      input          reference

                          D8       DVDD                power        power for digital circuit

                           E1         SID                       input       SCCB ID select input

                           E2       XVCLK                   input         system input clock

                           E3       EGND                ground       MIPI/PLL digital ground

                           E4       SCL                      input       SCCB input clock

                           E5       EGND                ground       MIPI/PLL digital ground

                           E6       TM                       input            test mode (active high with internal pull down resistor)

                           E7       VN2                      input          reference

                           E8       VH                       input          reference

                             F1      DOGND               ground       ground for I/O

                             F2       DVDD                power        power for digital circuit

                             F3       EVDD                power        MIPI/PLL digital power                             F4 ConfidentialDOGND               ground       ground for I/O

                             F5        MDN1/D7                I/O           MIPI negative data[1] / DVP output data[7]      Omnivision

                             F6      DOGND               ground       ground for I/O

                             F7       VN3                      input          reference            for
                             F8       AGND                ground       ground for analog circuit

                         G1       DVDD                power        power for digital circuit           only
                         G2      DOGND               ground       ground for I/O

                         G3       MDP0/D2                I/O           MIPI positive data[0] / DVP output data[2]

                         G4       MCP/D4                 I/O           MIPI positive clock / DVP output data[4]

                         G5       MDP1/D6                I/O           MIPI positive data[1] / DVP output data[6]

                         G6       DVDD                power        power for digital circuit

                         G7       AVDD                power        power for analog circuit





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 15

                                                                                                                                    1-3




      table 1-1      signal descriptions (sheet 3 of 3)

         pin                                pin
       number    signal name          type          description

       G8       AVDD                power        power for analog circuit

       H1       NC                  –            no connect

       H2       DOVDD              power        power for I/O circuit

       H3       MDN0/D3                I/O           MIPI negative data[0] / DVP output data[3]

       H4       MCN/D5                 I/O           MIPI negative clock / DVP output data[5]

       H5       EVDD                power        MIPI/PLL digital power

       H6       DOVDD              power        power for I/O circuit

       H7       AGND                ground       ground for analog circuit

       H8       NC                  –            no connect


      table 1-2      configuration under various conditions (sheet 1 of 2)

         pin                                                              after XSHUTDOWN
       number     signal name       XSHUTDOWNa      releaseb              software standbyc

       A4        SDA                     high-z                  input/open drain          input/open drain

       A5       XSHUTDOWN            input                    input                      input

                                                                                                    high-z by default
       A6        ILPWM                   high-z                  high-z by default
                                                                                                        (configurable)

                                                                                                    high-z by default
       B4        D8                       high-z                  high-z by default
                                                                                                        (configurable)        Confidential
                                                                                                    high-z by default
       B5         FSIN/VSYNC             high-z                  high-z by default
                                                                                                        (configurable)      Omnivision
                                                                                                    high-z by default
       B6        STROBE                 high-z                  high-z by default
                                                                                                        (configurable)            for                                                                                                    high-z by default
       C2        PCLK                    high-z                  high-z by default
                                                                                                        (configurable)

                                                                                                    high-z by default       C3         D1/GPIO1/ROI1          high-z                  high-z by defaultonly
                                                                                                        (configurable)

                                                                                                    high-z by default
       C4        D9                       high-z                  high-z by default
                                                                                                        (configurable)

       C6       ULPM                    high-z                  input/open drain          input/open drain

                                                                                                    high-z by default
       D1        HREF                    high-z                  high-z by default
                                                                                                        (configurable)





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 16

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 1-2      configuration under various conditions (sheet 2 of 2)

                                 pin                                                              after XSHUTDOWN
                          number     signal name       XSHUTDOWNa      releaseb              software standbyc

                                                                                                                              high-z by default
                          D4         D0/GPIO0/ROI0          high-z                  high-z by default
                                                                                                                                    (configurable)

                                                                                                                              high-z by default
                          D6        GPIO2/CONTEXT        high-z                  high-z by default
                                                                                                                                    (configurable)

                           E2        XVCLK                   input                    input                      input

                           E4        SCL                       input                    input                      input

                           E6       TM                        input                    input                      input

                                                                                                                              high-z by default
                            F5        MDN1/D7                high-z                  high-z by default
                                                                                                                                    (configurable)

                                                                                                                              high-z by default
                         G3        MDP0/D2                high-z                  high-z by default
                                                                                                                                    (configurable)

                                                                                                                              high-z by default
                         G4        MCP/D4                 high-z                  high-z by default
                                                                                                                                    (configurable)

                                                                                                                              high-z by default
                         G5        MDP1/D6                high-z                  high-z by default
                                                                                                                                    (configurable)

                                                                                                                              high-z by default
                          H3        MDN0/D3                high-z                  high-z by default
                                                                                                                                    (configurable)

                                                                                                                              high-z by default
                          H4        MCN/D5                 high-z                  high-z by default
                                                                                                                                    (configurable)

                                      a.  XSHUTDOWN = 0

                                      b.  XSHUTDOWN = 1

                                        c.  XSHUTDOWN = 1                                    standbyConfidentialinitiated by register      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 17

                                                                                                                                    1-5




      figure 1-1     pin diagram
                              OV9281
                                                A1       A2       A3       A4       A5       A6       A7       A8
                                             NC      DVDD    DOGND     SDA  XSHUTDOWN  ILPWM    DOGND     NC

                                                 B1       B2       B3       B4       B5       B6       B7       B8
                                             DVDD    DOGND    DVDD      D8    FSIN/VSYNC  STROBE   DOVDD    AVDD

                                                 C1       C2       C3       C4       C5       C6       C7       C8
                                              AVDD     PCLK  D1/GPIO1/ROI1  D9      DVDD     ULPM     ATEST    AGND

                                               D1       D2       D3       D4       D5       D6       D7       D8
                                                HREF     AGND    DOVDD D0/GPIO0/ROI0 PVDD GPIO2/CONTEXT  VN1     DVDD

                                                   E1       E2       E3       E4       E5       E6       E7       E8
                                                          SID     XVCLK    EGND      SCL     EGND     TM      VN2      VH

                                                    F1        F2        F3        F4        F5        F6        F7        F8
                                           DOGND    DVDD     EVDD    DOGND   MDN1/D7   DOGND     VN3     AGND

                                               G1       G2       G3       G4       G5       G6       G7       G8
                                             DVDD    DOGND   MDP0/D2   MCP/D4   MDP1/D6   DVDD     AVDD     AVDD

                                               H1       H2       H3       H4       H5       H6       H7       H8
                                             NC     DOVDD   MDN0/D3  MCN/D5    EVDD    DOVDD    AGND     NC


                                                                                                                                                    9281_CSP5_DS_1_1


      table 1-3     pad symbol and equivalent circuit (sheet 1 of 2)

        symbol                             equivalent circuit



                                                               PAD
        XVCLK

                                                       DOGND              EN


                                                               PAD                                                                      to core        Confidential
         SDA, ULPM                                                               from core          PD      OmnivisionDOGND

                                                                                                                   open-drain            for
                                                               PAD
        SCL                                                             PD

                                                       DOGND           only


                                                                                         DOVDD

                                                          DOUT
         FSIN/VSYNC, STROBE, ILPWM,                                                                      PAD
            D[9:0], GPIO[2:0], PCLK, HREF
                                                              EN
                                                                                                                                                         DIN

                                                                                                                  PD
                                                                                          DOGND





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 18

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 1-3     pad symbol and equivalent circuit (sheet 2 of 2)

                             symbol                             equivalent circuit


                                                                                             PAD

                               VN1, VN2, VN3

                                                                                   DOGND

                            MDP0, MDN0, MDP1, MDP1,                 PAD
                             MCP, MCN, EGND, AGND,
                           DGND, VH
                                                                                   DOGND


                                                                                             PAD

                             AVDD, EVDD, DVDD, DOVDD,
                         PVDD


                                                                                   DOGND



                                                                                                 PAD

                              TM, XSHUTDOWN, SID


                                                                                       DOGND        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 19

                                                                                                                                    2-1


  2  system level description

   2.1 overview


      The OV9281 image sensor is a low voltage, high performance 1/4-inch 1 megapixel b&w CMOS image sensor that
       provides the functionality of a single 1 megapixel (1280x800) camera using OmniPixel3-GS™ technology. The OV9281
       provides full-frame, sub-sampled, and windowed 8/10-bit MIPI images via the control of the Serial Camera Control Bus
      (SCCB) interface.

      The OV9281 has an image array capable of operating at up to 120 frames per second (fps) in 10-bit 1 megapixel
        resolution with complete user control over image quality, formatting and output data transfer.

        In addition, OmniVision image sensors use proprietary sensor technology to improve image quality by reducing or
        eliminating common lighting/electrical sources of image contamination, such as fixed pattern noise, smearing, etc., to
       produce a clean, fully stable image.

       For customized information purposes, the OV9281 includes 256 bits of one-time programmable (OTP) memory. The
      OV9281 has a one/two-lane MIPI interface.

   2.2 architecture


      The OV9281 sensor core generates streaming pixel data at a constant frame rate. figure 2-1 shows the functional block
       diagram.

      The timing generator outputs signals to access the image array. The entire pixel array is reset at the same point of time.
        After the exposure time has elapsed, the pixels stop gathering light and store the collected charge in a storage node.
      The charge then reads out row by row.

      The exposure time is controlled by adjusting the time interval between reset and transferring the charge to storage node.
        After the data of the pixels in the row has been sampled, it is processed through analog circuitry to correct the offset and        multiply the data with corresponding gain.ConfidentialFollowing analog processing is the ADC which outputs 10-bit data for each
         pixel in the array.      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 20

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                            figure 2-1    OV9281 block diagram

                                 OV9281

                                                         image sensor array                      output
                                                                                                             interface                                                     column
                                                      sample/hold
                                                                                                                                                                          MIPI      MCP/NMDP/N                                                                                                                                                                                  level                                                                                      10-bit                                                     image                                                                                                                       select                                                 AMP                                                                                                                                                               FIFO                 D[9:0]                                                             ADC                                                              array
                                                                                         VSYNC                                                           row                                                                          blackcalibration               DVP                                                                                              HREF
                                                                                                 PCLK
                                                                         gain control




                                                                                                      control register bank


                                      PLL  PLLs            timing generator         SCCB slave interface

                                                                                       XVCLK                                          XSHUTDOWN    CONTEXT    ROI[1:0]  FSIN   STROBE   ILPWM  ULPM TM          SID  SCL  SDA              9281_DS_2_1        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 21

                                                                                                                                    2-3




      figure 2-2    OV9281 MIPI reference design schematic



                                                                                                                  1                                                                                                                               2
                                                                                                 STROBE                                                                                                                  3                                                                                                                               4                                                                                        AGND  DOVDD EVDD  MCN/D5  MDN0D3  DOVDD                                  AGND
                                                                                              SDA         5            6   ULPM                                          H8 H7 H6 H5 H4 H3 H2 H1
                                                                                                    SCL          7            8   AVDD
                                                                                        XSHUTDOWN                                                                                                     EVDD                                                                                                                  9                                                                                                                              10                                                                NC4 AGND  DOVDD
                                                                                                                  11                                                                                                                              12                                                                                                                                                               MCN/D5  MDN0/D3  DOVDD NC3                                 A1                                                                     G8                                                                           AVDD                                                                                                                                                     SID                                   NC1                                                                AVDD                                                                                                                  13                                                                                                                              14                                 A2                    DVDD                                                                     G7                                                                           AVDD                                  DVDD                                                                AVDD                                                                                                                 MDP1                                                                                                                  15                                                                                                                              16                                 A3                   DOGND                                                                     G6                                                                         DVDD                                 DOGND                                                              DVDD                                                                                                              MDN1                                                                                                                                          24_5                                                                                                                  17                                                                                                                              18                                 A4                     SDA                                                                     G5                                                                          MDP1/D6                                                                                                                  DGND                                   SDA                                                              MDP1/D6                                                                                                                  19                                                                                                                              20                                 A5                                                                     G4                   XSHUTDOWN                                                                           MCP/D4                                                               MCP/D4                                                                                                                MCP                                 XSHUTDOWN                                                                                                                  21                                                                                                                              22                                 A6                                                                     G3                     ILPWM                                                                          MDP0/D2                                                                                                            MCN                                                              MDP0/D2                                                                                                         GPIO2                                   ILPWM                                                                                                                  23                                                                                                                              24                                 A7                                                                     G2                   DOGND                                                                      DOGND                                                                                                                                                                                                               HEADER                                                                                                                  DGND                                                           DOGND                                                                                                         GPIO1                                 DOGND                                                                                                                  25                                                                                                                              26                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     FX12B-40S-0.4SV>                                 A8                                                                     G1                                                                         DVDD                                                                                                                                                                         JP1                                                                                                                 MDP0                                                              DVDD                                                                                                         GPIO0                                   NC2                                                                                                                  27                                                                                                                              28                                  B1                    DVDD                                                                            F8                                                                        AGND                                                                                                              MDN0                                  DVDD                                                                                              ILPWM                                                             AGND                                                                                                                  29                                                                                                                              30                   DOGND                                  B2                                                                            VN3                                                                                                                                             <HRS                                 DOGND                                                                                                                  DGND                                                                 VN3 F7                                                                                                                  31                                                                                                                              32                                  B3                    DVDD                                                                            F6                                                                      DOGND                                                                                                                             XVCLK                                  DVDD                                                                                                       FSIN/VSYNC                                                           DOGND                                                                                                                              34                                                                                                                  33                                                   U2                                  B4                     D8                                                                            F5  MDN1/D7                                                                                                                     DVDD                                   D8                                                             MDN1/D7                                                                                                                  35                                                                                                                              36                               OV9281                                  B5                       FSIN/VSYNC                                                                            F4                                                                      DOGND                                                                                                                  DOVDD                                      FSIN/VSYNC                                                           DOGND                                                                                                                  37                                                                                                                              38                                CSP5                                  B6                     STROBE                                                                            F3                                                                           EVDD                                                                                        DGND                                                                                                                  DGND                                    STROBE                                                               EVDD                                                                                                                              40                                                                                                                  39                                  B7                    DOVDD                                                                            F2                                                                         DVDD                                 DOVDD                                                              DVDD
                    AVDD        B8 AVDD                       DOGND F1    DOGND
                    AVDD        C1 AVDD                          VH E8       VH
                      PCLK        C2 PCLK                          VN2 E7      VN2
                       D1/GPIO1     C3 D1/GPIO1                      TM E6      TM
                     D9          C4 D9                           EGND E5     EGND       DVDD EVDD DOVDD PVDD             AVDD  VH   VN1  VN2  VN3
                                  C5                    DVDD                                                                                  SCL                                                                      SCL E4                                  DVDD                                                                                                               R14      0-0603                                  C6                    ULPM                                                                           E3     EGND                                 ULPM                        EGND
                       ATEST        C7 ATEST                         XVCLK E2     XVCLK
                    AGND        C8 AGND                                 SID E1        SID                                                                                                                                                                                                                                                                                                                                                                                                                                                            0.1μF-0603                 0.1μF-0603                 0.1μF-0603                 0.1μF-0603                                                        1μF-0603             1μF-0603             1μF-0603             1μF-0603             1μF-0603
                                                                                     HREF AGND  DDOVDD   D0/GPIO0 PVDD  GPIO2 VN1 DVDD                                       C22     C23     C24     C25                     C13     C14     C15     C16     C17
                                          D1 D2 D3 D4 D5 D6 D7 D8
                                                                                                                                                                                                       DGND                                       AGND
                                                                                   HREF AGND  DOVDD   D0/GPIO0 PVDD  GPIO2 VN1 DVDD

              note 1 XSHUTDOWN is active low and should be connected to DOVDD outside of module if unused.

              note 2 AVDD and PVDD is 2.7 ~ 3.0V of sensor analog power (clean).

              note 3 DOVDD is 1.7 ~ 3.0V of sensor digital IO power (clean). 1.8V is recommended.              note 4 EVDD and DVDD is 1.2V ±5%.  Confidential
              note 5 sensor AGND and DGND should be separated inside the module and connected to a single point outside the module (do not be connected inside module).              note 6 capacitors should be close to their relatedOmnivisionsensor pins.

              note 7 EVDD/EGND are power/ground for MIPI core.
               MCP and MCN are MIPI clock lane positive and negative output.
                MDP0/MDP1 and MDN0/MDN1 are MIPI data lane positive and negative output.              note 8 traces of MCP, MCN, MDPx, and MDNx should have the same or similar length.     for
                         differential impedance of the clock pair and data pair transmission line should be controlled at 100 Ohm.

              note 9 SID is SCCB slave address selection
                  when this pin pulled to ground, SCCB slave address is 0xC0.
                  when this pin pulled to DOVDD, SCCB slave address is 0x20.                                                                                                              9281_CSP5_DS_2_2A           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 22

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                            figure 2-3    OV9281 DVP reference design schematic




                                                                                                                                                                                                      1                                                                                                                                                                             PWR                 no mount unless requested                         1μF
                                                                                                                                                                                      4                                                                                                                                                                                                      3                                                                                                                                                                                                                                     AGND  DOVDD EVDD  MCN/D5  MDN0/D3  DOVDD                                             2                                                                                                                                                                                                                                                                                                                                                                                       C18                                                                                                                                                                                      6                                                                                                                                                                                                      5
                                                                                                                H8 H7 H6 H5 H4 H3 H2 H1                              D0/GPIO0     8               7       XVCLK             PWR                                14 15
                                                                                                                                                                                     10                                                                                                                                                                                                      9                                                                                                                                                                                D1/GPIO1                                                                                                                                                                                                                                                           CP                                                                                                                                                                                                                                                              CN                                                                                                                                                                                                                                                             13                                                                                                                                                                                                                                                                                    VIN                                                                                                                                                                                                                                                                                         3                                                                                                                                                                                                                                                                                             C21                                                                                                                                                                                                                                                                                                                       1μF                                                                                                                                                                                     12                                                                                                                                                                                                      11        SDA                                                                                                                                                                                                                                       R19      100K                                                                                                                                                                                                                                                              8                                                                                                                                                                                                                                                                     CAP                                                                                                                                                                                                                                                       FLAG                                                                                                                                                                         NC4 AGND  DOVDD EVDD  MCN/D5  MDN0/D3  DOVDD NC3                                                                                                                                              AVDD                                                                                                       A1                                                                                                                                           G8                                                                                                                                                                                                                                                                                         4                                                                                                                                                                                                      13                                                                                                                                                                                                              SCL                     1μF                                                                                                                                                               MDP0/D2                                                                                                                                                                                     14                                                                                                                                                                                                                                                              5                                                                                                        NC1                                                                                                                                    AVDD                                                                                                                                                                                                                                                                     CAP                                                                                                                                                                                                                                 CHARGE                                                                                                                                                                                                                                      CHRG                                                                                                                                                                                                                                                      U5                                                                                       DVDD                                                                                                                                              AVDD                                                                                                                                                                                                                                                                                             C20                                                                                                                                                                                                                                                                                                                  470μF                                                                                                       A2                                                                                                                                           G7                                                                                                                                                                                                                                                                                         2                                                                                                                                                                                     16                                                                                                                                                                                                      15                                                                                                                                                            MDN0/D3                                                                                                                                                                                                                                                              6                                                                                                                                    AVDD                                                                                                                                                                                                                                                                          BAL                                                                                                    DVDD                                                                                                                                                                                                                                              STRB                                                                                                                                                                                                                                                                                                                                  C19                                                                                                                                                                                                                                                                          CAT3224                                                                                                                                                                                                                                                       FLASH                                                                                    DOGND                                                                                                                                          DVDD                                                                                                       A3                                                                                                                                           G6                                                                                                                                                                                                                                                                                         10                                                                                                                                                                                     18                                                                                                                                                                                                      17                                                                                                                                                                                                                                                              7                                                                                                 DOGND                                                                                                                                DVDD                                                                                                                                                                                                                                                                           LEDA                                                                                                                                                                                                                                   TORCH                                                                                                                                                                                                                                            TORCH                                                                                          SDA                                                                                                                                                                                                                                                                                             C25                                                                                                                                                                                                                                                                                                                       1μF                                                                                                                                               MDP1/D6                                                                                                       A4                                                                                                                                                                                     20                                                                                                                                                                                                      19                                                                                                                                           G5                                                                                                                                                                                                                                                                                         9                                                                                                                                                               MCP/D4                                                                                                                                                                                                                                                             11                                                                                                       SDA                                                                                                                                    MDP1/D6                                                                                                                                                                                                                                              RC                                                                                                                                                MCP/D4                                                                                    XSHUTDOWN                                                                                                                                           G4                                                                                                       A5                                                                                                                                                            MCN/D5                                                                                                                                                                                     22                                                                                                                                                                                                      21                                                                                                                                                                                                                                                           RT                                                                                                                                                                                                                                                                                                                                                                                            GND                                                                                                                                                                                                                                                                                                                                                                                                 TAB   LEDB                                                                                                                                     MCP/D4                                                                                                 XSHUTDOWN                                                                                                                            1RF                                                                                                                                               MDP0/D2                                                                                                                                                                                     24                                                                                                                                                                                                      23                                                                                          ILPWM                                                                                                                                           G3                                                                                                       A6                                                                                                                                                                                                                                                          12                                                                                                                                                                                                                                                             16                                                                                                                                                                                                                                                               17                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             PBGND                                                                                                                                    MDP0/D2                                                                                                        ILPWM
                                                                                                                                     DOGND                                                                                                                                                                                     26                                                                                                                                                                                                      25                                                                                    DOGND                                                                                                       A7                                                                                                                                           G2                                                                                                                                                               MDP1/D6
                                                                                                                                      2                                                                                                                           DOGND                                                                                                 DOGND                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     NC(0)                                                                                                               0                                                                                                                                                                                                                                                                                                                                                  10K                                                                                                                                                                                                                                                                                                                                                      10K                                                                                                                                                                                                                                                                                                                                                          560                                                                                                                            0                                                                                                                                                                                                                                                                                                                                                                                       500                                                                                                                                          DVDD                                                                                                                                                                                     28                                                                                                                                                                                                      27                                                                                                       A8                                                                                                                                           G1                                                                                                                                                            MDN1/D7                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    QTE-020-01-L-D-A                                                                                                                                DVDD                                                                                                        NC2
                                                                                                                                                                                   J5                                                                                       DVDD                                                                                                         B1                                                                                                                                                                                     30                                                                                                                                                                                                      29                                                                                                                                         AGND                                                                                                                                                       F8                                                                                                    DVDD                                                                                                                               AGND                                                                                                                                                                                                                                                                                                   3                                                                                    DOGND                                                                                                         B2                                                                                                                                               VN3                                                                                                                                                                                     32                                                                                                                                                                                                      31                                                                                                                                                       F7                                                                                                                                                              D8                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       SFH4235                                                                                                 DOGND                                                                                                                                    VN3                                                                                                                                                                                                                                                                                                                                          R23                                                                                                                                                                                                                                                                                                                                              R24                                                                                                                                                                                                                                                                                                                                                  R26                                                                                                                                                                                                                                                                                                                                                      R27                                                                                                                                                                                                                                                                                                                                                          R28                                                                                                                                                                                                                                                                                                                                                                                   R29                                                                                                                                                                                                                                                                                                                                                                                       R30                                                                                       DVDD                                                                                                         B3                                                                                                                                                                                     34                                                                                                                                                                                                      33                                                                                                                                     DOGND                                                                                                                                                       F6                                                                                                                                                              D9                                                                                                    DVDD                                                                                                                           DOGND
                                                                                                         B4                                                                                                                                                                                     36                                                                                                                                                                                                      35                                                                                                                                            MDN1/D7                                                                                                                                                                                                                 PCLK_CNT               DOGND                                                                                                                                                       F5                                                                                           D8                                                                                                                                                                                                                                                                           D1                                                                                                         D8                                                                                                                       U2         MDN1/D7
                                                                                                                                      1                                                                           OV9281                                                                                                                                                                                     38                                                                                                                                                                                                      37                                                                                                   FSIN/VSYNC                                                                                                         B5                                                                                                                                                       F4                                                                                                                                     DOGND                                                                                                                                                                                 HREF_GPIO                                                                                                                           DOGND                                                                                                                 FSIN/VSYNC                                                                              CSP5                                                                                                         B6                                                                                                                                                                                     40                                                                                                                                                       F3                                                                                                                                             EVDD                                                                                                                                                                                                      39  STROBE_CNT                                                                                             STROBE                                                                                                                                                                              VSYNC_FSIN                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   STROBE                                                                                                           STROBE                                                                                                                                   EVDD
                                                                                                         B7                                                                                                                                                       F2                                                                                                                                          DVDD                                                                                      DOVDD                                                                                                  DOVDD                                                                                                                                DVDD                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 PBGND                                                                                                                                                                              41                                                                                                                                                                                 42                                                                                                                                                                                    43                                                                                                                                                                                      44                                                                                                         B8                                                                                                                                                       F1                                                                                                                                     DOGND                                                                                         AVDD                                                                                                       AVDD                                                                                                                           DOGND
                                                                                                                                                                                                                             U1                                                                                                          C1                                                                                                                                                     E8                                                                                         AVDD                                                                                                       AVDD                                                                                                                                 VH                                                                                                                                                                                        PWR                                                                                                                                                                                                                                            ADP123-SOT23                                                                                                                                                                                                                                                                                      AVDD                                                                                                                                                                                    20x2/SM                                                                                                          C2                                                                                                                                                     E7                                                                                                                                               VN2                                                                                                PCLK                                                                                                              PCLK                                                                                                                                    VN2                                                                                                                                                                                                                                          1                                                                                                                                                                                                                                                                                    R1      0K-0603                                                                                                                                                                                                                                                      5                                                                                                                                           VH         DGND          HEADERMIPI                                                                                                                                                                                                                                        traces                                                                                                                                                                                                                                    should                                                                                                                                                                                                                             have                                                                                                                                                                                                      same                                                                                                                                                                                                                                                                length                                                                                                                                                                                                                                                                                            IN                                                                                                          C3                                                                                                                                                                                                                           OUT                                                                                                                                                     E6                                                                                                                                  TM                                                                                                     D1/GPIO1                                                                                                                        TM                                                                                                                    D1/GPIO1                                                                                                                                                                                                                                          2                                                                                                                                                                                                                                                      4                                                                                                                                                                                                      and with                                                                                                                                                                                        100                                                                                                                                                                                 ohm                                                                                                                                                                                                                     impedance                                                                                                                                                                                                                                                                     control                                                                                                                                                                                                             GND                                                                                                          C4                                                                                                                                                     E5                                                                                                                                                                                                                                               FB                                                                                                                                           EGND                                                                                           D9                                                                                                         D9                                                                                                                                 EGND                                                                                                                                                                                                                                          3                                                                                                                                                                                                                                                                      EN_                                                                                                                                                     E4                                                                                                          C5                                                                                                                                                           SCL                                                                                       DVDD                                                                                                                                               SCL                                                                                                    DVDD
                                                                                                                                                     E3                                                                                                          C6                                                                                                                                           EGND                                                                                     ULPM                                                                                                                                 EGND                                                                                                  ULPM
                                                                                                                                                     E2                                                                                                          C7                                                                                                                                                    XVCLK                                                                                                    ATEST                                                                                                                                         XVCLK                                                                                                                  ATEST                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              0-0603                                                               5pF-0603                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              46Kohm-0603                                                                                   10μF-0603                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       0.1μF-0603
                                                                                                                                                     E1                                                                                                                                                                                SID                                                                                                                                                                                                                                                                         22μF-0603                                                                                      AGND       C8                                                                                                                                                                   SID                                                                                                   AGND
                                                                                                                                                                                                                  C1                                                                                                                                                                                                                                            C2                                                                                                                                                                                                                                                                      C3                                                                                                                                                                                                                                                                           C4
                                                                                                                                                                                                                                 HREF AGND  DOVDD   D0/GPIO0 PVDD  GPIO2 VN1 DVDD          DOVDD   R13      0-0603   SID     R14      0-0603        DOGND             R3                     R2  R4      10Kohm-0603
                                                                                                                                                                                      R16                                                                                                                                                                                                                 0-0603                                                                                                                                                             R15                                                                                                                                                                                      0-0603                                                                                                                                                   TM                                                                                                                                                                                      DOGND                                                                                                                D1 D2 D3 D4 D5 D6 D7 D8           DOVDD
                                                                                                                                                             R17                                                                                                                                                                                      0-0603                                                                                                                                                                                      R18                                                                                                                                                                                                                 0-0603                                                                                                                                                                                      ATEST                                                                                                                                                                                      DOGND             DOGND                                                                                                              AGND                                                                                                                                      DOVDD


                                                                                                                                                             R20                                                                                                                                                                                      0-0603        EVDD                                                                                                                                                                                                                               HREF AGND  DOVDD   D0/GPIO0 PVDD  GPIO2 VN1 DVDD           DVDD
                                                                                                                                                                                      0-0603                                                                                                                                        DVDD     R21                                                                                                                                                                        EVDD                           PWR         ADP123-SOT23U3                                                     DOVDD
                                                                                                                                                             R22                                                                                                                                                                                      0-0603   STROBE_CNT                                                                                                                                                 STROBE                                                                                                                                                                                                                                          1                                                                                                                                                                                                                                                                                    R5      0K-0603                                                                                                                                                                                                                                                      5                                                                                                                                                                                                                                                                                            IN                                                                                                                                                                                                                           OUT                                                                                                                                                                                      1-0603                                                                                                                                                                         PVDD                                                                                                                                            AVDD     R25                                                                                             AVDD  VH   VN1  VN2  VN3  DVDD EVDD DOVDD                                                                                                                                                                                                                                          2                                                                                                                                                                                                                                                      4                                                                                                                                                                                                             GND                                                                                                                                                                                                                                               FB
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               0.1μF-0603                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 22μF-0603                  0-0603 3 EN_                                          5pF-0603             39K-0603                                                                                   10μF-0603                 0.1μF-0603                                                                                                                                                                                                                                                                                                                                                                                               1μF-0603             1μF-0603             1μF-0603             1μF-0603             1μF-0603                 0.1μF-0603                 0.1μF-0603                 0.1μF-0603
                                                                                                                                                                                                                                                         C26                                          C5                        C6                        C7   C8
                                                                                                                                               C13     C14     C15     C16     C17     C22     C23     C24                                                                                   R7                     R6
                                                                                                                                                                                                                                                                                                                                                                                                                               DOGND                                                                              R8      15K-0603
                                                                                                                                                                                               AGND                                                              DOGND                      DOGND                                                                                                                                                                                           DOGND                                                                                                                                          DOGND

                                                                                                                                                        T1         VH                                                                     U4
                                                                                                                            J1                                                                                                                                                      J3                                                                                                                                                                                        PWR                                                                                                                                                                                                                                            ADP123-SOT23                                                                                                                                                                                                                                                                              DVDD
                                                                                              HEADER2X1                                                                                                                  HEADER2X1                                                                                                                                                   VN1                                                                                                                                                                                                                                          1                                                                                                                                                                                                                                                                                    R9      0K-0603                                                                                                                                                                                                                                                      5                                                                                                                                                        T2                                                                                                                                                                                                                                                                                            IN                                                                                                                                                                                                                           OUT                                                                                                             HREF                                                                                                                                    FSIN/VSYNC                                                                                                                                                                                                                                          2                                                                                                          1                                                                                                                               1                                                                                                                                                                                                                                                      4                                                                                                                                                                                                             GND                                                                                                                                                                                                                                               FB                                                                                                                                      VSYNC_FSIN                                                                                                                                                   VN2                                                                                                                                                                                                                                          3                                                                                                          2   HREF_GPIO                                                                                                                               2                                                                                                                                                        T3                                                                                                                                                                                                                                                                      EN_

                                                                                                                            J2
                                                                                              HEADER2X1                                                                                                                  HEADER3X1                                                                                                                                                      J4                       T4        VN3                                 DOGND       AGND        PBGND                                                                                                                                22μF-0603                  0-0603                                                               5pF-0603             21K-0603                                                                                   10μF-0603                 0.1μF-0603
                                                                                                                                            ULPM                                                                                                          1                                                                                                                PCLK         1                                                                                                                      DOVDD          T5
                                                                                                                                                          AGND                                                                                                                                                                                                        and                                                                                                                                                              DOGND                                                                                                                                                                                                                                         are                                                                                                                                                                                                                                      sensor                                                                                                                                                                                                                                                 analog                                                                                                                                                                                                                                 and digital                                                                                                                                                                                                                                                                         ground.      C9                                                                                                                                                                                                                                                                                                                                                                  C10                                                                                                                                                                                                                                                                                                                                                                                                         C11     C12                                                                                                                XSHUTDOWN                                                                                                          2    PCLK_CNT                                                                                                                               2                                                                                                                                                                                                                                                                                                                                       R11                                                                                                                                                             PBGND                                                                                                                                                                                                                                                                                              is                                                                                                                                                                                  LED                                                                                                                                                                                                                                                             driver                                                                                                                                                                                                                                                ground.                                                                                                                    DOGND                                                                                                                                                                                                                                                                                                                                                                          R10 R12      15K-0603                                                                                                                               3                                                                                                                                                        T6      ILPWM                                                                                                                                                                                                               connect                                                                                                                                                                                                                                                 different                                                                                                                                                                                                                          ground                                                                                                                                                                                                                                                          plants                                                                                                                                                                                                                                                       to a single                                                                                                                                                                                                                                                                                                    point.
                                                                                                                                                        T7        GPIO2                                                                                                                                        DOGND                                                                                                                                          DOGND


                                                                                                                                                                                                                                                                                                                                                                                                                     9281_CSP5_DS_2_2B

                    2.3 format and frame        Confidential                           TheOmnivisionOV9281 supports RAW output with a 2-lane MIPI interface.                           table 2-1     supported resolution and frame rate for

                                formata            resolutionb      max frame rate    methodology         typical MIPI data rate                                                    full resolution      1280x800         120 fpsonlyfull                    2-lane @ 800Mbps

                              720p             1280x720         130 fps              cropping              2-lane @ 800Mbps

                        VGA             640x480          180 fps              cropping              2-lane @ 800Mbps

                              640x400          640x400          210 fps               4:1 sub-sampling      2-lane @ 800Mbps

                                      a.    all formats with minimum four dummy lines and four dummy pixels

                                      b.   supports zero row overhead time (ROT) readout





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 23

                                                                                                                                    2-5




      2.3.1 MIPI interface

      The OV9281 supports a one/two-lane MIPI transmitter interface with a data transfer rate of up to 800 Mbps per lane.

      figure 2-4     MIPI timing

                                                                (1)

         VSYNC
                               (2)      (3)       (4)                       (6)
                                                                                                              (5)
                                   (8)


                                    (7)                                                                (9)

          MDP B8 S0 S1 S2 S3 Tr   B8 T0 T1 T2 T3 data CRC0 CRC1 Tr   B8 SS0 SS1 SS2 SS3 Tr
          MDN
                         short package                     long package                      short package
                     S0: start frame                     T0: data type                   SS0: end frame
                     S1: frame cnt LSBs                 T1: word cnt LSBs               SS1: frame cnt LSBs
                     S2: frame cnt MSBs                 T2: word cnt MSBs               SS2: frame cnt MSBs
                     S3: ECC                            T3: ECC                        SS3: ECC
                                              CRC0: CRC LSBs
                                              CRC1: CRC MSBs                                            9281_9282_DS_2_3


      table 2-2     MIPI timing specifications (2-lane mode)

       mode                                      timing

                                                                  (1) 663,208 tp
                                                                  (2) 1,024 tp
                                                                  (3) 12,159 tp        Confidential(4) 791 tp
                                                                  (5) 65,665 tp
                full resolution      Omnivision(6) 674 tp         1280x800
                                                                   (7) 33 tp
                                                                  (8) -65 tp
                                                                   (9) 37 tp            for
                                                  where tp = Tsclk

                                                                  (1) 234,688 tp                                                                  (2) 1,024 tp  only
                                                                  (3) 6,496 tp
                                                                  (4) 772 tp
                                                                  (5) 42,422 tp
         640x400                                             (6) 216 tp
                                                                   (7) 22 tp
                                                                  (8) -194 tp
                                                                   (9) 9 tp

                                                  where tp = Tsclk





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 24

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                               2.3.2 VSYNC timing in MIPI mode

                           The VSYNC rising edge delay is controlled by register vsync_delay ({0x4314, 0x4315, 0x4316}) in all three VSYNC
                             modes. VSYNC width is controlled by register vsync_width_pixel ({0x4311, 0x4312}) for VSYNC modes 1 and 2. The
                                 steps of both registers vsync_delay and vsync_width_pixel are 1 system clock cycle.

                              Note that VSYNC timing in mode 3 is a long VSYNC mode. The register vsync_width_pixel ({0x4311, 0x4312}) controls
                       VSYNC falling edge differently.

                                        2.3.2.1 VSYNC mode 1
                                          In mode 1, VSYNC is generated by the internal start of frame (SOF) signal (see figure 2-5).

                            figure 2-5    VSYNC timing in mode 1



                                                      pclk


                                                                                                                                            can be adjusted


                                     VSYNC

                                                                                                                                can be adjusted

                                         SOF


                                                                                                                                                                                                                                                                                                       9281_9282_DS_2_4


                                        2.3.2.2 VSYNC mode 2
                                          In mode 2, VSYNC is generated by the internal end of frame (EOF) signal (see figure 2-6).

                            figure 2-6    VSYNC timing in mode 2



                                                      pclk

                                                                                            can be adjusted

                                          EOF


                                                                                                                                   can be adjusted                                     VSYNC Confidential

                                                                                                                                                                                                                                                                                                       9281_9282_DS_2_5      Omnivision

                                        2.3.2.3 VSYNC mode 3
                                          In mode 3, VSYNC is generated by EOF and SOF (see figure 2-7).            for

                            figure 2-7    VSYNC timing in mode 3           only
                                                      pclk

                                                                                                can be adjusted

                                          EOF





                                     VSYNC
                                                                                                                                                            can be adjusted


                                         SOF


                                                                                                                                                                                                                                                                                                       9281_9282_DS_2_6





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 25

                                                                                                                                    2-7



   2.4 I/O control

      table 2-3     I/O control registers

          function                           register          description

                                                                                   Bit[6:5]:  I/O pin drive capability
                                                                             00:  1x
          output drive capability control       0x3001                       01:  2x
                                                                             10:  3x
                                                                                 11:  4x

                                                                                      Bit[3]:    input/output control for STROBE pin
       STROBE I/O control              0x3006                          0:    input
                                                                                     1:   output

                                                                                      Bit[3]:    output selection for STROBE pin
       STROBE output select            0x3027                          0:   normal data path
                                                                                     1:    register control value

       STROBE output value             0x3009                 Bit[3]:   STROBE output value

                                                                                      Bit[2]:    input/output control for ILPWM pin
       ILPWM I/O control                0x3006                          0:    input
                                                                                     1:   output

                                                                                      Bit[2]:    output selection for ILPWM pin
       ILPWM output select              0x3027                          0:   normal data path
                                                                                     1:    register control value

       ILPWM output value              0x3009                 Bit[2]:  PWM output value

                                                                                      Bit[1]:    input/output control for FSIN pin
        FSIN/VSYNC I/O control           0x3006                          0:    input
                                                                                     1:   output

                                                                                      Bit[1]:    output selection for FSIN pin
        FSIN/VSYNC output select         0x3027                          0:   normal data path        Confidential1:    register control value        FSIN/VSYNC output valueOmnivision0x3009                 Bit[1]:   FSIN output value            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 26

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    2.5 power management

                               2.5.1 power up sequence

                           The OV9281 has no internal regulator.


                           table 2-4     power up sequence

                            case   DVDD   XSHUTDOWN   power up sequence requirement

                                                                          Refer to figure 2-8
                                                                                       1. AVDD rising can occur before or after DOVDD rising as long as they
                              1         external   GPIO                  are rising before XSHUTDOWN rising
                                                                                       2. XSHUTDOWN is pulled up after AVDD and DOVDD are stable
                                                                                       3. DVDD rises after DOVDD, but before XSHUTDOWN is pulled high



                           table 2-5     power up sequence timing constraints

                                  constraint                               label     min          max                unit

                         AVDD rising – DOVDD rising                t0                                  ms
                                                                           0            ∞
                       DOVDD rising – AVDD rising                t1                                  ms

                         AVDD or DOVDD rising, whichever is
                                                                                       t2        1                           ms
                                           last – XSHUTDOWN rising

                       XSHUTDOWN rising – first SCCB
                                                                                       t3        8192                         XVCLK cycles
                                      transaction

                             minimum number of XVCLK cycles
                                                                                       t4        8192                         XVCLK cycles
                                          prior to the first SCCB transaction        Confidential
                            PLL start up/lock time                        t5                              0.2            ms      Omnivisionentering streaming mode – first frame
                                                                                       t6                         10            ms
                                           start sequence (fixed part)

                                     entering streaming mode – first frame                                                                                        t7          delay is theforexposure time value          lines                                           start sequence (variable part)

                       DOVDD to external DVD rising              t8        0                           ms           only
                       DOVDD rising to XSHUTDOWN rising       t9        0                           ms





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 27

                                                                                                                                    2-9




      figure 2-8    power up sequence


                         power     hardware
                 STATE    off       standby          software standby             streaming (active)
                                      note 1

              DOVDD
                                      t8


               AVDD
       (AVDD rising first)


               DVDD


                               t0    t1

               AVDD
      (DOVDD rising first)                     t9                                                                                           t6 (fixed)


         XSHUTDOWN                                                      t7
                                                                                       (variable)
                                                      t3

               XVCLK
               (free running)

               XVCLK
                     (gated)


                      XVCLK may either be free running or gated.
                            the requirement is that XVCLK must be active for time t4 prior to the first SCCB transaction.


                 SDA


                                                                 t4                    t5        Confidential
                  SCL      Omnivision

           MDP/MDN            for
        note 1 with low power consumption                                                                                         9281_9282_DS_2_7           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 28

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                               2.5.2 power down sequence

                           The digital and analog supply voltages can be powered down in any order (e.g., DOVDD, then AVDD or AVDD, then
                          DOVDD). Similar to the power-up sequence, the XVCLK input clock may be either gated or continuous. If the SCCB
                         command to exit streaming is received while a frame of MIPI data is being output, then the sensor must wait to the MIPI
                               frame end code before entering software standby mode.

                                                        If the SCCB command to exit streaming mode is received during the inter frame time, then the sensor will enter software
                               standby mode immediately.


                           table 2-6     power down sequence

                            case   DVDD   XSHUTDOWN   power down sequence requirement

                                                                          Refer to figure 2-9
                                                                                       1.  software standby recommended
                              1         external   GPIO                 2.  pull XSHUTDOWN low for minimum power consumption
                                                                                       3.  pull DVDD low
                                                                                       4. AVDD and DOVDD may fall in any order



                           table 2-7     power down sequence timing constraints

                                  constraint                                label    min           max                unit

                                                                      when a frame of MIPI data is output,
                                                                                              wait for the MIPI end code before
                                     enter software standby SCCB command
                                                                                        t0         entering the software for standby;
                                   device in software standby mode
                                                                                           otherwise, enter the software standby
                                                                  mode immediately

                             minimum of XVCLK cycles after the last
                                                                                        t1       512                          XVCLK cycles                        SCCB transactionConfidentialor MIPI frame end      Omnivisionlast SCCB transaction or MIPI frame         t2       512                          XVCLK cycles
                                  end, XSHUTDOWN falling

                       XSHUTDOWN falling - AVDD falling or
                                                                                        t3        0.0                          ms                       DOVDD falling whichever is first    for

                         AVDD falling - DOVDD falling                 t4      AVDD and DOVDD may fall in any     ms
                                                                                                 order, the falling separation can vary                       DOVDD falling - AVDD falling                 t5 onlyfrom 0 ns to infinity               ms

                       XSHUTDOWN falling - DVDD falling         t6       0                           ms

                        DVDD falling to DOVDD falling               t7       0                           ms





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 29

                                                                                                                              2-11




      figure 2-9    power down sequence


                                                                                hardware
               STATE           streaming (active)             software standby     standby    power off
                                                                                   note 1

            DOVDD

                                                                                              t7     t5


             DVDD

                                                                                          t4

             AVDD




             AVDD


                                                                                      t6


       XSHUTDOWN

                                                                      t2

             XVCLK
            (free running)

             XVCLK
                  (gated)


                                                      XVCLK may either be free running or gated.
                            the requirement is that XVCLKmust be active for time t1 after the last SCCB transaction
                                              or after the MIPI frame end short packet, whichever is the later event.        Confidential

               SDA      Omnivision
                                            t0                t1                SCL              if SCCB command received during the readout of the frame then the forsensor must wait
                                   after the MIPI frame end short packet before entering sleep mode. if the SCCB
                          command is received during the inter frame time the sensor must enter sleep mode
                                 immediately.           only
          MDP/MDN


          note 1 with low power consumption                                                                                  9281_9282_DS_2_8





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 30

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                            figure 2-10   standby sequence


                                     STATE          streaming (active)                         software standby


                                  SDA
                                                                   t1

                                     SCL          SCCB command received during the readout of the frame then the sensor must wait
                                                                   after the MIPI end of frame short packet before entering software standby mode.
                                                     enter
                                                  software

                            MCP/MCN                                                  LP-11 (high)

                                 MIPI
                            MDP/MDN                                                  LP-11 (high)



                                     STATE               streaming (active)                         software standby


                                  SDA

                                                                                      t1

                                     SCL                                     SCCB command is received during the inter
                                                                                          frame time sensor enters software standby
                                                                                            enter
                                                                             mode immediately.
                                                                                    software

                            MCP/MCN                                                       LP-11 (high)

                                 MIPI
                            MDP/MDN                                                       LP-11 (high)
                                                                                                                                                                                       9281_9282_DS_2_9        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 31

                                                                                                                              2-13



   2.6 reset


      The OV9281 sensor includes a XSHUTDOWN pin (pin A5) that forces a complete hardware reset when it is pulled low
       (GND). The OV9281 clears all registers and resets them to their default values when a hardware reset occurs.

      2.6.1 power ON reset generation

      The OV9281 has a power on reset that is generated after the core power becomes stable.

   2.7 hardware and software standby


      Two suspend modes are available for the OV9281:

          •   hardware standby
          •   software standby


      table 2-8     hardware and software standby description

       mode                                               description

                                                                          1.  enabled by pulling XSHUTDOWN pad low
                                                                          2. power down all blocks
          hardware standby with XSHUTDOWN                      3.  register values are reset to default values
                                                                          4. no SCCB communication
                                                                          5. minimum power consumption

                                                                          1.  default mode after power on reset
                                                                          2. power down all blocks except SCCB
                                                                          3.  register values are maintained
           software standby
                                                                          4. SCCB communication is available
                                                                          5.  low power consumption
                                                                          6. GPIO can be configured as high/low/tri-state        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 32

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    2.8 system clock control


                            The OV9281 has two on-chip PLLs which generate the system clock from a 6~27 MHz input clock. A programmable clock
                                      divider is provided to generate different frequencies for the system.

                               2.8.1 PLL configuration

                           PLL settings can only be changed during sensor standby mode (0x0100 = 0).

                            figure 2-11   PLL1 clock diagram


                                                                                                                                                multiplier   500 ~ 1200 MHz                                                                                  pre_div0                                                   6 ~ 64 MHz                                                                                                            pre_div     4 ~ 27 MHz                                                                                                                                                             M_div                                                                                                                                                                                              MIPI_div       PLL1 pix clk                                           XVCLK                                                                                                                                        {0x0301[1:0],                                                                         0x030A[0]                                                                                                0x0300[2:0]                                                                                                                                                                                  0x0304[1:0]                                                                                                                                                             0x0303[3:0]
                                                                                                                                  0x0302[7:0]}
note
Contact your local                                                                                                                                    0x0305[1:0]sys_pre_div           0x0306[0]sys_div        PLL1 sys clk
OmniVision FAE for                                                                                                                                                                                                                                                      9281_9282_DS_2_10
additional assistance
on PLL configuration.
                            figure 2-12   PLL2 clock diagram


                                                                                                                                                multiplier   500 ~ 1200 MHz                                                                                  pre_div0                                                   6 ~ 64 MHz                                                                                                            pre_div     4 ~ 27 MHz                                                                                                                                                       ADC_div        PLL2 ADC clk                                           XVCLK                                                                                                                                       {0x030C[1:0],                                                                          0x0314[0]                                                                                               0x030B[2:0]                                                                                                                                                             0x0313[3:0]
                                                                                                                                0x030D[7:0]}


                                                                                                                                                                          ana_div         PLL2 analog clk
                                                                                                                                                             0x0312[3:0]


                                                                                                                                                                             sys_pre_div              sys_div        PLL2 sys clk
                                                                                                                                                              0x030F[3:0]          0x030E[2:0]

                                                                                                                                                                                                                                                                                              9281_9282_DS_2_11


                           table 2-9    PLL control registers (sheet 1 of 2)        Confidential
                                   function                  address      description      Omnivision
                                                                                                             Bit[0]:   PLL1 pre divider 0
                                  PLL1_pre_div0             0x030A                      0:    /1
                                                                                                          1:    /2            for
                                                                                                         Bit[2:0]:  PLL1 pre divider
                                                                                           000: /1
                                                                                           001: /1.5           only
                                                                                           010: /2
                                  PLL1_pre_div               0x0300                    011: /2.5
                                                                                           100: /3
                                                                                           101: /4
                                                                                              110: /6
                                                                                                  111: /8

                                      PLL1_multiplier             0x0301             Bit[1:0]:  PLL1_multiplier[9:8]

                                      PLL1_multiplier             0x0302             Bit[7:0]:  PLL1_multiplier[7:0]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





   Powered by TCPDF (www.tcpdf.org)

---

## Page 33

                                                                                                                              2-15




      table 2-9    PLL control registers (sheet 2 of 2)

          function                  address      description

                                                                         Bit[1:0]:  PLL1 system pre divider
                                                                    00:  /3
          PLL1_sys_pre_div           0x0305                    01:  /4
                                                                    10:  /5
                                                                        11:   /6

                                                                           Bit[0]:   PLL1 system divider
          PLL1_sys_div              0x0306                      0:    /1
                                                                            1:    /2

                                                                         Bit[3:0]:  PLL1_M_div
         PLL1_M_div                0x0303
                                                                                                                  / (1 + 0x0303[3:0])

                                                                         Bit[1:0]:  PLL1 MIPI divider
                                                                    00:  /4
          PLL1_MIPI_div             0x0304                    01:  /5
                                                                    10:  /6
                                                                        11:   /8

                                                                           Bit[0]:   PLL2 pre divider 0
          PLL2_pre_div0              0x0314                      0:    /1
                                                                            1:    /2

                                                                         Bit[2:0]:  PLL2 pre divider
                                                                 000: /1
                                                                 001: /1.5
                                                                 010: /2
          PLL2_pre_div              0x030B                    011: /2.5
                                                                 100: /3
                                                                 101: /4
                                                                    110: /6
                                                                       111: /8

           PLL2_multiplier             0x030C             Bit[1:0]:  PLL2_multiplier[9:8]        Confidential
           PLL2_multiplier             0x030D             Bit[7:0]:  PLL2_multiplier[7:0]      Omnivision
                                                                         Bit[3:0]:  PLL2 system pre divider
          PLL2_sys_pre_div           0x030F
                                                                                                                  / (1 + 0x030F[3:0])                                                                         Bit[2:0]:  PLL2 system divider for
                                                                 000: /1
                                                                 001: /1.5
                                                                 010: /2           only          PLL2_sys_div              0x030E                    011: /2.5
                                                                 100: /3
                                                                 101: /3.5
                                                                    110: /4
                                                                       111: /5

                                                                         Bit[3:0]:  PLL2 ADC divider
         PLL2_ADC_div             0x0313
                                                                                                                  / (1 + 0x0313[3:0])

                                                                         Bit[3:0]:  PLL2 analog divider
         PLL2_ana_div              0x0312
                                                                                                                  / (1 + 0x0312[3:0])





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 34

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                           table 2-10    sample PLL configurationa

                        name                        address                  value

                                 PLL1_pre_div0                   0x030A[0]                    1’h0

                                 PLL1_pre_div                     0x0300[2:0]                  3’h1

                                       PLL1_multiplier[9:8]                0x0301[1:0]                  2’h0

                                       PLL1_multiplier[7:0]                0x0302[7:0]                 8’h32

                                 PLL1_sys_pre_div                 0x0305[1:0]                  2’h2

                                 PLL1_sys_div                     0x0306[0]                    1’h1

                               PLL1_M_div                       0x0303[3:0]                  4’h0

                                 PLL1_MIPI_div                    0x0304[1:0]                  2’h3

                                 PLL2_pre_div0                    0x0314[0]                    1’h0

                                 PLL2_pre_div                     0x030B[2:0]                  3’h4

                                       PLL2_multiplier[9:8]               0x030C[1:0]                  2’h0

                                       PLL2_multiplier[7:0]               0x030D[7:0]                 8’h50

                                 PLL2_sys_pre_div                 0x030F[3:0]                  4’h3

                                 PLL2_sys_div                     0x030E[2:0]                  3’h2

                              PLL2_ADC_div                    0x0313[3:0]                  4’h1

                                PLL2_ana_div                     0x0312[3:0]                  4’h7

                           SYS_CLK                                              80 MHz

                              MIPI_PCLK                                            100 MHz

                              MIPI_CLK                                              800 Mbps        Confidential
                          EXTCLK                                               24 MHz      Omnivision                                      a.  PLL control for 1 megapixel @ 120 fps with 2-lane, 10-bit output            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 35

                                                                                                                              2-17




      figure 2-13    clock connection diagram


                                      column
                                        readout

                                                          MCP
                                      image                        MCN                                                                                 select                                                                   ISP            FIFO       MIPI      MDP[1:0]                                             array
                                        row                                           MDN[1:0]



                                    timing generator
                                                                                               SCLK                          MIPI_PCLK        MIPI_CLK      9281_9282_DS_2_12



      table 2-11   PLL speed limitation

         parameter                                       value

           PLL1_multiplier input                              4~27 MHz

           PLL1_multiplier output                             500~1200 MHz

           PLL2_multiplier input                              4~27 MHz

           PLL2_multiplier output                             500~1200 MHz

        SYS_CLK                                       up to 80 MHz        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 36

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    2.9 serial camera control bus (SCCB) interface


                           The Serial Camera Control Bus (SCCB) interface controls the image sensor operation. Refer to the OmniVision
                                Technologies Serial Camera Control Bus (SCCB) Specification for detailed usage of the serial control port.

                           The OV9281 responds to two SCCB ID set by register SC_SCCB_ID1 (default 0xC0) and SC_SCCB_ID2 (default 0xE0).
                         One of them can be used as a broadcasting ID and the other one can be programmed to a unique ID.

                               2.9.1 data transfer protocol

                              Data transfer of the OV9281 follows SCCB protocol.

                               2.9.2 message format

                           The OV9281 supports the message format shown in figure 2-14. The repeated START (Sr) condition is not shown in
                       SCCB single read from random location, but is shown in SCCB single read from current location and SCCB sequential
                               read from random location.

                            figure 2-14   message type

                              message type: 16-bit sub-address, 8-bit data, and 7-bit slave address


                                            slave             sub address      sub address
                            S         R/W  A            A            A      data     A/A  P                                      address                  [15:8]                [7:0]


                                                                 index[15:8]         index[7:0]

                                   from slave to master                S   START condition           A   acknowledge

                                   from master to slave                P  STOP condition            A   negative acknowledge                                          directionConfidentialdepends on operation          Sr  repeated START condition                            9281_9282_DS_2_13      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 37

                                                                                                                              2-19




      2.9.3 read / write operation

      The OV9281 supports four different read operations and two different write operations:

          •   a single read from random locations
          •   a sequential read from random locations
          •   a single read from current location
          •   a sequential read from current location
          •   single write to random locations
          •   sequential write starting from random location

      The sub-address in the sensor automatically increases by one after each read/write operation.

        In a single read from random locations, the master does a dummy write operation to desired sub-address, issues a
       repeated start condition and then addresses the camera again with a read operation. After acknowledging its slave
       address, the camera starts to output data onto the SDA line as shown in figure 2-15. The master terminates the read
        operation by setting a negative acknowledge and stop condition.

      figure 2-15   SCCB single read from random location


                             previous index value, K                                    index M                index M + 1



                 slave            sub address      sub address               slave
        S           0  A            A            A   Sr             1  A    data   A  P              address                [15:8]                [7:0]               address



                                          index value M                                                                                                    9281_9282_DS_2_14


             If the host addresses the camera with read operation directly without the dummy write operation, the camera responds       by setting the data from last used sub-addressConfidentialto the SDA line as shown in figure 2-16. The master terminates the read
        operation by setting a negative acknowledge and stop condition.      Omnivision
      figure 2-16   SCCB single read from current location            for
                previous index value, K                               index K + 1                         index K + 2           only

                 slave                                                    slave
        S           1  A    data   A  P          S             1  A    data   A  P              address                                            address


                                                                                                                                                                                           9281_9282_DS_2_15





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 38

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology





                           The sequential read from a random location is illustrated in figure 2-17. The master does a dummy write to the desired
                                 sub-address, issues a repeated start condition after acknowledge from slave and addresses the slave again with read
                                   operation. If a master issues an acknowledge after receiving data, it acts as a signal to the slave that the read operation
                                      shall continue from the next sub-address. When master has read the last data byte, it issues a negative acknowledge
                            and stop condition.

                            figure 2-17   SCCB sequential read from random location


                                                            previous index value, K                                  index M                           index      index
                                                                                 M + L - 1   M + L



                                                 slave            sub address      sub address               slave
                                S           0  A            A            A   Sr             1  A    data   A          data   A  P
                                           address                [15:8]                [7:0]               address




                                                                        index value M                                          L bytes of data              9281_9282_DS_2_16


                           The sequential read from current location is similar to a sequential read from a random location. The only exception is
                                     that there is no dummy write operation, as shown in figure 2-18. The master terminates the read operation by setting a
                                 negative acknowledge and stop condition.

                            figure 2-18   SCCB sequential read from current location

                                            previous index value, K         index K + 1                                     index        index                                                                                            K + L - 1     K + L



                                            slave
                            S           1  A    data   A    data   A                                data   A  P                                       address        Confidential      OmnivisionL bytes of data                                                    9281_9282_DS_2_17            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 39

                                                                                                                              2-21





      The write operation to a random location is illustrated in figure 2-19. The master issues a write operation to the slave,
        sets the sub-address and data correspondingly after the slave has acknowledged. The write operation is terminated with
      a stop condition from the master.

      figure 2-19   SCCB single write to random location


                           previous index value, K                      index M       index M + 1




                  slave            sub address      sub address
         S           0  A            A            A    data   A/A  P
               address                [15:8]                [7:0]




                                           index value M                                                                                       9281_9282_DS_2_18


      The sequential write is illustrated in figure 2-20. The slave automatically increments the sub-address after each data
        byte. The sequential write operation is terminated with stop condition from the master.

      figure 2-20   SCCB sequential write to random location

                           previous index value, K                      index M                 index       index                                                    M + L - 1   M + L



                  slave            sub address      sub address
         S           0  A            A            A    data   A           data   A/A  P               address                [15:8]                [7:0]



                                           index value M                   L bytes of data                       9281_9282_DS_2_19        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 40

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                               2.9.4 SCCB timing

                            figure 2-21   SCCB interface timing

                                                                      tF         tHIGH            tR


                                        SCL

                                                                  tLOW                                                                                                           tSU:STO                                                                                                  tSU:DAT                                                                      tHD:STA

                                  SDA (IN)   tSU:STA

                                                           tAA           tHD:DAT                               tBUF


                                 SDA (OUT)

                                                                                                                                                               9281_9282_DS_2_20
                                                                             tDH

                           table 2-12   SCCB interface timing specificationsab

                             symbol      parameter                            min        typ      max         unit

                                      fSCL           clock frequency                                                400        kHz

                                 tLOW          clock low period                                1.3                               µs

                                      tHIGH          clock high period                               0.6                               µs

                                      tAA        SCL low to data out valid                       0.1                       0.9         µs        Confidential
                                     tBUF         bus free time before new start                   1.3                               µs      Omnivision
                                      tHD:STA         start condition hold time                        0.6                               µs

                                       tSU:STA         start condition setup time                       0.6                               µs            for
                                      tHD:DAT       data in hold time                          0                                 µs

                                      tSU:DAT       data in setup time                              0.1                               µs           only                                     tSU:STO       stop condition setup time                       0.6                               µs

                                             tR, tF       SCCB rise/fall times                                                      0.3         µs

                                    tDH           data out hold time                             0.05                              µs

                                      a.  SCCB timing is based on 400kHz mode

                                      b.   timing measurement shown at the beginning of the rising edge or the end of the falling edge signifies 30%,
                                        timing measurement shown in the middle of the rising/falling edge signifies 50%, timing measurement shown at the
                                     beginning of the falling edge or the end of the rising edge signifies 70%





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 41

                                                                                                                              2-23




      2.9.5 group write and fast mode switching

      Group write is supported in order to update a group of registers in the same frame. These registers are guaranteed to
      be written prior to the internal latch at the frame boundary. Group write can be used to switch modes quickly.


      table 2-13    context switching control

                                                default
        address     register name          value    R/W      description

                                                             Group Access
                                                                                              Bit[7:4]:  group_ctrl
                                                                               0000:  Group hold start
                                                                               0001:  Group hold end
                                                                               1010:  Group launch
                                                                                    1110:   Immediate launch
        0x3208    GROUP ACCESS       –     W                      others: Reserved
                                                                                              Bit[3:0]:  group_id
                                                                               0000:  Group bank 0, default start
                                                                                       from address 0x00
                                                                               0001:  Group bank 1, default start
                                                                                       from address 0x40
                                                                                           others: Reserved

         0x3209     GRP0_PERIOD         0x00    RW      Frames For Staying in Group 0

        0x320A     GRP1_PERIOD         0x00    RW      Frames For Staying in Group 1

                                                                                                 Bit[3]:    group_switch_repeat
        0x320B    GRP_SWCTRL          0x01    RW             Bit[2]:   Group switch enable
                                                                                              Bit[1:0]:  Second group selection

        0x320D    GRP_ACT             –       R          Indicates Which Group is Active

        0x320E     FRAME_CNT_GRP0     –       R         frame_cnt_grp0        0x320F     FRAME_CNT_GRP1Confidential–       R         frame_cnt_grp1      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 42

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology

        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 43

                                                                                                                                    3-1


  3  block level description

   3.1 pixel array structure


      The OV9281 sensor has an image array of 1296 columns by 816 rows (1,057,536 pixels). figure 3-1 shows a
        cross-section of the image sensor array.

       Of the 1,057,536 pixels, 1,024,000 (1280x800) are active pixels and can be output. The other pixels are used for black
         level calibration and interpolation.

      figure 3-1     sensor array region color filter layout


                                                  columns
               0 1... 7 8 9 10 11 12... 14 15    1280 1281 1282 1283 1284 1285 1286 1287    1288 1289...  1295

                       0 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                                                                                   1... P3P1 P4P2 P3P1 P4P2   P3P1 P4P2 P3P1 P4P2 P3P1 P4P2 P3P1 P4P2   P3P1 P4P2 P3P1 P4P2 P3P1 P4P2 P3P1 P4P2   P3P1 P4P2 P3P1 P4P2    dummy
                       7 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4

                       8 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                       9 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4
                      10 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                      11 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4
                      12 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                                                                                                     active                                         rows   13 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4                                                                                                           pixel
                     802 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                     803 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4
                     804 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                     805 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4
                     806 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2
                     807 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4

                     808 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2                     809 P3 P4 P3 P4   P3ConfidentialP4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4   dummy
                                                              ... P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2 P1 P2 P1 P2   P1 P2 P1 P2                     815 P3 OmnivisionP4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4 P3 P4 P3 P4   P3 P4 P3 P4



                                                             active
                       dummy                                                             dummy for                                                                 pixel

                                                                                                                                                                        9281_9282_DS_3_1           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 44

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    3.2 subsampling


                              There are two subsampling modes in the OV9281: binning and skipping. Both are acceptable methods of reducing output
                                    resolution while maintaining the field of view. Binning is usually preferred as it increases the pixel’s signal-to-noise ratio.
                        When the binning function is ON, voltage levels of a pair of the same pixels (e.g., P1 and P1 pixels, or P2 and P2 pixels)
                                are averaged. In skipping mode (binning function is OFF), alternate pixels, which are not output, are merely skipped. The
                          OV9281 supports 2x2 binning. figure 3-2 illustrates 2x2 binning, where the voltage levels of two horizontal (2x1) pixels,
                               which are the same, such as P3 and P3 pixels are averaged.

                            figure 3-2    example of 2x2 binning





                                                                                       P00   P01   P02   P03
                                                                                                                     P00 + P01     P02 + P03
                                                                                                                                       + P10 + P11     + P12 + P13

                                                                                       P10   P11   P12   P13            2            2





                                                                                       P20   P21   P22   P23
                                                                                                                     P20 + P21     P22 + P23
                                                                                                                                       + P30 + P31     + P32 + P33

                                                                                       P30   P31   P32   P33            2            2

                                                                                                                                                                                                                                                                 9281_9282_DS_3_2

                            figure 3-3    example of 2:1 skipping


                                                                                             P00  P01  P02  P03          P00      P02

                                                                                             P10  P11  P12  P13

                                                                                             P20  P21  P22  P23          P20      P22

                                                                                             P30  P31  P32  P33        Confidential9281_9282_DS_3_3      Omnivision
                            figure 3-4    example of 4:1 skipping


                                                                               P00  P01  P02  P03  P04  P05  P06  P07           P00                P04                                                        ...    for
                                                                                                                              P40                P44
                                                                               P20  P21  P22  P23  P24  P25  P26  P27
                                                                               ...                                                 9281_9282_DS_3_4           only

                           table 3-1      binning-related registers

                                                                               default
                             address       register name              value    R/W      description

                               0x3820      TIMING_FORMAT1          0x00    RW            Bit[1]:    Vertical binning

                               0x3821      TIMING_FORMAT2          0x00    RW            Bit[0]:    Horizontal binning





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 45

                                                                                                                                    4-1


  4  image sensor core digital functions

   4.1 mirror and flip


      The OV9281 provides mirror and flip readout modes, which respectively reverse the sensor data readout order
        horizontally and vertically (see figure 4-1).

      figure 4-1     mirror and flip samples
         F   F  F   F

                 original image            mirrored image             flipped image          mirrored and flipped
                                                                                      image





                                                                                                                                                                                  9281_9282_DS_4_1



      table 4-1     mirror and flip registers

                                                    default
        address       register name            value     R/W      description

                                                                           Timing Control Register
                                                                                                        Bit[2]:    Vertical flip enable
        0x3820      IMAGE_ ORIENTATION     0x00     RW
                                                                                                      0:   Normal
                                                                                                     1:    Vertical flip        ConfidentialTiming Control Register
                                                                                                        Bit[2]:    Horizontal mirror enable        0x3821      IMAGE_ OmnivisionORIENTATION     0x00     RW                      0:   Normal
                                                                                                     1:   Horizontal mirror            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 46

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    4.2 image windowing


                          An image windowing area is defined by four parameters, horizontal start (HS), horizontal end (HE), vertical start (VS),
                            and vertical end (VE). By properly setting the parameters, any portion within the sensor array size can output as a visible
                                  area. Windowing is achieved by simply masking off the pixels outside of the window; thus, the timing is not affected.

                            figure 4-2    image windowing

                                                       (0, 0)                               sensor array size X

                                          (HS, VS)                     HW





                                       VH
                                      sensor array
                                                 size Y



                                                                                      valid pixel (windowing) size                       (HE, VE)





                                                                                                    sensor array size

                                                                                                                                                                                                                                 9281_9282_DS_4_2                           table 4-2Confidentialimage windowing control functions      Omnivision                                   function                   register           R/W               description

                                                                                                              HS[9:8]  = 0x3800
                                        horizontal start             {0x3800, 0x3801}     RW
                                                                                                              HS[7:0]  = 0x3801            for
                                                                                                                VS[9:8]  = 0x3802
                                           vertical start               {0x3802, 0x3803}     RW
                                                                                                                VS[7:0]  = 0x3803           only                                                                                                              HE[9:8]  = 0x3804
                                        horizontal end             {0x3804, 0x3805}     RW
                                                                                                              HE[7:0]  = 0x3805

                                                                                                                VE[9:8]  = 0x3806
                                           vertical end                {0x3806, 0x3807}     RW
                                                                                                                VE[7:0]  = 0x3807





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 47

                                                                                                                                    4-3



   4.3 test pattern


       For testing purposes, the OV9281 offers three test patterns:

      4.3.1 general test pattern bar

      figure 4-3     test pattern





      table 4-3     general test pattern bar selection control

         function                   register       default value    R/W      description

         general test pattern bar    0x5E00      0x00         RW             Bit[7]:    Test pattern bar enable


      4.3.2 solid color test pattern


      table 4-4      solid color test pattern control (sheet 1 of 2)         function            register Confidentialdefault value    R/W      description      OmnivisionBit[7:6]:  Pixel order
                                                                                           00:  P3P4/P1P2
                                                                                           01:  P4P3/P2P1
                                                                                           10:  P1P2/P3P4            for
                                                                                                11:  P2P1/P4P3
           solid test pattern    0x4320        0x80         RW
                                                                                                        Bit[1]:    Solid test pattern enable
                                                                                                     0:    Solid test pattern OFF           only1:    Solid test pattern
                                                                                         enable
                                                                                                       Bit[0]:   Debug control

           solid pattern P1     0x4322        0x00         RW             Bit[1:0]:  solid_testpattern_P1[9:8]

           solid pattern P1     0x4323        0x00         RW             Bit[7:0]:  solid_testpattern_P1[7:0]

           solid pattern P2     0x4324        0x00         RW             Bit[1:0]:  solid_testpattern_P2[9:8]

           solid pattern P2     0x4325        0x00         RW             Bit[7:0]:  solid_testpattern_P2[7:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 48

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 4-4      solid color test pattern control (sheet 2 of 2)

                                  function            register        default value    R/W       description

                                         solid pattern P4     0x4326        0x00         RW             Bit[1:0]:  solid_testpattern_P4[9:8]

                                         solid pattern P4     0x4327        0x00         RW             Bit[7:0]:  solid_testpattern_P4[7:0]

                                         solid pattern P3     0x4328        0x00         RW             Bit[1:0]:  solid_testpattern_P3[9:8]

                                         solid pattern P3     0x4329        0x00         RW             Bit[7:0]:  solid_testpattern_P3[7:0]


                    4.4 black level calibration (BLC)


                           The pixel array contains several optically shielded (black) lines. These lines are used as reference for black level
                                       calibration.

                                Black level adjustments can be made with registers 0x4000, 0x4001, 0x4002, 0x4003, 0x4004 and 0x4009.


                           table 4-5    BLC control functions (sheet 1 of 2)

                                                                     default
                             address     register name      value    R/W     description

                                                                                                                          Bit[0]:   BLC enable
                               0x5000      ISP CTRL00        0x9F    RW                    0:   Disable
                                                                                                                     1:   Enable

                                                                                                                      Bit[7:4]:  r_off_avg_weight_o
                                                                                                                          Bit[3]:    r_target_adj_dis_o
                               0x4000     BLC_CTRL_00     0xCF    RW           Bit[2]:   r_off_cmp_en_o
                                                                                                                          Bit[1]:    r_dither_en_o
                                                                                                                          Bit[0]:   r_mf_en_o        Confidential
                                                                                                                      Bit[7:6]:  r_hdr_option_o
                                                                                                                          Bit[5]:   r_kcoef_man_en_o      Omnivision
                                                                                                                          Bit[4]:   r_off_man_en_o
                               0x4001     BLC_CTRL_01      0x20    RW
                                                                                                                          Bit[3]:    r_zero_in_out_en_o
                                                                                                                          Bit[2]:    r_blk_in_out_en_o                                                                                                                      Bit[1:0]:forr_byp_mode_o

                                                                                                                      Bit[7:3]:  Reserved
                               0x4002     BLC_AUTO         0x00    RW
                                                                                                                      Bit[2:0]:  Black target level[10:8]           only
                               0x4003     BLC_CTRL_03      0x10    RW           Bit[7:0]:  Black target level[7:0]

                                                                                                                      Bit[7:4]:  Reserved
                               0x4008     BLC_CTRL_08      0x00    RW
                                                                                                                      Bit[3:0]:  r_up_bl_start_o[3:0]

                                                                                                                      Bit[7:4]:  Reserved
                               0x4009     BLC_CTRL_09      0x07    RW
                                                                                                                      Bit[3:0]:  r_up_bl_end_o[3:0]

                                                                                                                      Bit[7:4]:  Reserved
                             0x400C     BLC_CTRL_0C      0x00    RW
                                                                                                                      Bit[3:0]:  r_dn_bl_start_o[3:0]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 49

                                                                                                                                    4-5




      table 4-5    BLC control functions (sheet 2 of 2)

                                            default
        address     register name      value    R/W     description

                                                                                      Bit[7:4]:  Reserved
        0x400D     BLC_CTRL_0D      0x07    RW
                                                                                      Bit[3:0]:  r_dn_bl_end_o[3:0]

                                                                                        Bit[7]:    r_up_off_trig_en_o
                                                                                        Bit[6]:    r_gain_chg_trig_en_o
                                                                                        Bit[5]:    r_fmt_chg_trig_en_o
                                                                                        Bit[4]:    r_rst_trig_en_o
         0x4010     BLC_CTRL_10      0x41    RW
                                                                                         Bit[3]:   r_man_avg_en_o
                                                                                        Bit[2]:    r_man_trig_o
                                                                                        Bit[1]:    r_off_frz_en_o
                                                                                        Bit[0]:    r_off_always_up_o

                                                                                        Bit[7]:   Reserved
                                                                                        Bit[6]:   r_off_chg_mf_en_o
                                                                                        Bit[5]:   r_fmt_chg_mf_en_o
                                                                                        Bit[4]:   r_gain_chg_mf_en_o
         0x4011     BLC_CTRL_11      0x7F    RW
                                                                                        Bit[3]:   r_rst_mf_mode_o
                                                                                        Bit[2]:   r_off_chg_mf_mode_o
                                                                                        Bit[1]:   r_off_chg_mf_mode_o
                                                                                        Bit[0]:   r_gain_chg_mf_mode_o



      table 4-6    ALS algorithm control registers (sheet 1 of 3)

                                            default
        address    register name       value    R/W    description

                                                                                       Bit[5]:   Range level adjust enable
                                                                                       0:   Disable
                                                                                       1:   Enable        Confidential
                                                                                       Bit[4]:   Merge average option enable      Omnivision0:   Disable
                                                                                       1:   Enable
                                                                                       Bit[3]:   Merge enable
                                                                                       0:   Disable
        0x4E00    ALS CTRL 00        0x00    RW                                                                                       1:   Enable for
                                                                                       Bit[2]:   Reserved
                                                                                       Bit[1]:    Select abs value enable
                                                                                       0:   Disable           only
                                                                                       1:   Enable
                                                                                       Bit[0]:   ALS enable
                                                                                       0:   Disable
                                                                                       1:   Enable

                                                                                       Bit[7]:    Clock select
        0x4E01    ALS CTRL 01        0x01    RW
                                                                                    Bit[6:0]:  Sample period intervals

        0x4E02    ALS CTRL 02        0x00    RW          Bit[7:0]:  Manual threshold[15:8]

        0x4E03    ALS CTRL 03        0x00    RW          Bit[7:0]:  Manual threshold[7:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 50

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 4-6    ALS algorithm control registers (sheet 2 of 3)

                                                                     default
                             address    register name       value    R/W    description

                             0x4E04    ALS CTRL 04        0x00    RW          Bit[7:0]:  Small zone manual threshold[15:8]

                             0x4E05    ALS CTRL 05        0x00    RW          Bit[7:0]:  Small zone manual threshold[7:0]

                             0x4E06    ALS CTRL 06        0x00    RW          Bit[7:0]:  Large zone manual threshold[15:8]

                             0x4E07    ALS CTRL 07        0x00    RW          Bit[7:0]:  Large zone manual threshold[7:0]

                                                                                                                        Bit[0]:    Threshold manual enable
                             0x4E08    ALS CTRL 08        0x00    RW                    0:   Disable
                                                                                                                    1:   Enable

                             0x4E09    ALS CTRL 09        0x00    RW          Bit[7:0]:  Percentage step

                            0x4E0A    ALS CTRL 0A        0x00    RW          Bit[7:0]:  Light meter x0[15:8]

                            0x4E0B    ALS CTRL 0B        0x00    RW          Bit[7:0]:  Light meter x0[7:0]

                            0x4E0C    ALS CTRL 0C        0xFF    RW          Bit[7:0]:  Light meter x1[15:8]

                            0x4E0D    ALS CTRL 0D        0xFF    RW          Bit[7:0]:  Light meter x1[7:0]

                            0x4E0E    ALS CTRL 0E        0x00    RW          Bit[7:0]:  Analog register control[15:8]

                             0x4E0F    ALS CTRL 0F        0x23    RW          Bit[7:0]:  Analog register control[7:0]

                                                                                                                        Bit[7]:    Interrupt trigger enable
                                                                                                                    0:   Disable
                                                                                                                    1:   Enable
                                                                                                                        Bit[6]:   Reserved
                                                                                                                        Bit[5]:    Interrupt_sel
                             0x4E10    ALS CTRL 10        0x9A    RW
                                                                                                                        Bit[4]:    ready_intr_pol
                                                                                                                        Bit[3]:    threshold_intr_pol
                                                                                                                        Bit[2]:   Reserved
                                                                                                                        Bit[1]:   Out range interrupt enable        Confidential
                                                                                                                        Bit[0]:   Host clear format      Omnivision                                                                                                                        Bit[7]:   Ready_opt
                              0x4E11    ALS CTRL 11        0x02    RW
                                                                                                                     Bit[6:0]:  Mask period number

                             0x4E12    ALS CTRL 12        0x01    RW          Bit[7:0]:  Interrupt number control interrupt width            for
                                                                                                                     Bit[6:2]:  Range adjust
                             0x4E20    ALS CTRL 20        –       R             Bit[1]:   Reserved
                                                                                                                        Bit[0]:    Light meter zone1           only

                             0x4E21    ALS CTRL 21        –       R      Zone 1 Light Meter Statistics High Byte

                             0x4E22    ALS CTRL 22        –       R      Zone 1 Light Meter Statistics Low Byte

                             0x4E23~
                                RSVD              –         –       Reserved
                             0x4E24

                             0x4E25    ALS CTRL 25        –       R      Zone 1 Light Meter Statistics High Byte

                             0x4E26    ALS CTRL 26        –       R      Zone 1 light Meter Statistics Low Byte





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 51

                                                                                                                                    4-7




      table 4-6    ALS algorithm control registers (sheet 3 of 3)

                                            default
        address    register name       value    R/W    description

        0x4E27~
               RSVD              –         –       Reserved
        0x4E28

                                                                                       Bit[3]:   Out range input
                                                                                       Bit[2]:    Trigger all
        0x4E29    ALS CTRL 29        –       R
                                                                                       Bit[1]:   Reserved
                                                                                       Bit[0]:   Zone 1 trigger

                                                                                       Bit[3]:    poll_thre_i
                                                                                       Bit[2]:    poll_rdy_i
        0x4E2A    ALS CTRL 2A        –       R
                                                                                       Bit[1]:    intr_thres_i
                                                                                       Bit[0]:    intr_rdy_i

                                                                                       Bit[1]:    host clear[1]
                                                                             host_rdy_clear
        0x4E30    ALS CTRL 30        –      RW
                                                                                       Bit[0]:    host clear[0]
                                                                              host_thres_clear


   4.5 one time programmable (OTP) memory


      The OV9281 has 256-bit embedded one time programmable (OTP) memory. The OTP memory can be programmed and
       read back via SCCB bus. This document provides general guidelines for programming and accessing the OTP memory.

      4.5.1 OTP memory structure

      128 bits of OTP memory are reserved for OmniVision internal use. These bits are usually used to store the production
        information or used by some internal functions. The remaining 127 bits are fully user programmable. The user can store
        production tracking information, camera module calibration data, etc. to these bits.        Confidential      table 4-7    OTP memoryOmnivisionstructure

      OTP bits                                            function           [127:0]                                                reserved by OmniVision for internalforuse

          [128]                                   OTP write disable          [255:129]                                             user programmableonly


      4.5.2 accessing the OTP memory

      The OTP memory cannot be directly accessed. Instead, it is accessed through its register buffer 0x3D00~0x3D1F as
      shown in figure 4-4. Registers 0x3D80 and 0x3D81 are the command registers to program values to and read values
       back from OTP memory.





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 52

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology





                        When 0x01 is programmed to register 0x3D81, the OTP memory controller will load the content of all OTP memory bits
                                      to its corresponding register buffer. After that, the user can read the OTP content from its register buffer. It is
                           recommended to clear the register buffer to zero before loading the OTP.

                        When value 0x01 is programmed to register 0x3D80, the OTP memory controller will program the data of register
                           0x3D00~0x3D1F to its corresponding OTP memory bits. Keep in mind, the memory is one time programmable. It cannot
                            be programmed back to 0 once it is programmed to 1. In fact, the OTP memory controller only programs those bits with
                                 value 1 in its corresponding memory buffer when programming command is issued. Multi-pass programming is allowed.
                              However, programming 1 to an OTP bit that is already programmed to 1 in previous pass is prohibited. The user should
                               always program a bit from 0 to 1 only in any programming pass.

                        When register 0x3D10[0] (corresponding to [128] in OTP memory) is programmed to 1 through the SCCB or loaded from
                       OTP (please note that waking up OV9281 will automatically load OTP to buffer), all register buffers (0x3D00~0x3D1F)
                                      writing operations are disabled. This is a security feature in the OV9281 which can protect OTP from a potential hacking
                                 breach. Please be advised that you can still burn OTP memory by programming register 0x3D80 to 0x01 even if register
                               0x3D10[0] is set to 1.

                       OTP access is in system clock domain, so register 0x100 has to be set to 1 to enable system clock PLL in order to access
                            OTP. The OTP programming pulse width is controlled by register 0x3D82 and the unit is 8 system clock periods. The
                                     default value of 0x65 is for a 48MHz system clock and the programming pulse width is 16.8µs. The OTP read pulse width
                                           is set by register 0x3D83 and the default value of 0x05 gives 104ns at 48MHz system clock. When the system clock
                                frequency is different, the programming pulse should set to the closest value to 10µs and should be greater than 9µs.
                           The system clock frequency is dependent on the input clock and PLL configuration. Refer to section 2.8 for details.

                        When the OTP memory controller is programming data to OTP memory or reading data from OTP memory, the sensor
                                             will not respond to any SCCB access. Because OTP programming current is quite high, accessing sensor register is
                                    prohibited in order to prevent any glitch on the power supply. It is recommended to wait 15ms after issuing the OTP read
                            and program command. This delay should be scaled with the system clock period.        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 53

                                                                                                                                    4-9




      figure 4-4    OTP access


                                 7      6      5      4      3      2      1      0


                   0x3D00[7:0]
                                15     14     13     12     11     10      9      8


                   0x3D01[7:0]



                                247    246    245    244    243    242    241    240


                   0x3D1E[7:0]
                                255    254    253    252    251    250    249    248


                   0x3D1F[7:0]


              0x3D80~0x3D84                      OTP memory controller


                                                                                                                                         9281_9282_DS_4_4


      4.5.3 procedure for accessing OTP memory

       Since the OTP memory can only be programmed once, the user should be very careful when accessing the OTP. Here
          is a detailed procedure for OTP access.

      4.5.4 procedure to read OTP content

             1.   Clear software buffer which is to receive the OTP content.

             2.   Configure PLL and set register 0x100 to 1 if not yet set.

             3.   Clear register buffer 0x3D00~0x3D1F to 0x00.

             4.   Set register 0x3D81 to 0x01.

             5.   Wait 15ms.        Confidential
             6.   Read register 0x3D00~0x3D1F and set to the software buffer.      Omnivision
      The OTP read operation is performed to verify the OTP memory is blank before program data to it, or to verify OTP
       contents after programming data to it.            for
        Verifying the OTP content at the last step of camera module testing is highly recommended in case the OTP content is
        accidently overwritten during the module testing.           only
      4.5.5 procedure to program OTP content

             1.   Follow procedure to read OTP content to make sure the OTP to be programmed is blank.

             2.   Program the intended OTP content to its corresponding register buffer, and clear unused register buffer to 0.

                   a.   For customer - registers 0x3D00~0x3D0F must be cleared to 0x00 before initiating the OTP
                   programming command

             3.   Read back registers 0x3D00~0x3D1F to make sure they are the correct data to program to OTP memory or 0
                    for all other bits.

             4.    Write 0x01 to register 0x3D80 to initiate OTP programming.





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 54

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology





                                          5.   Wait 15ms, any register access during this period is prohibited.

                                          6.   Follow procedure to read OTP content to read back the OTP content.

                                          7.   Compare the OTP content read back to the intended OTP content.

                               4.5.6 power supply requirement for OTP memory programming

                           The OTP memory is programmed using the analog power. The AVDD voltage for OTP programming must be 2.6V ~ 3.0V.
                           The power supply should be able to provide extra 50mA for OTP programming.

                    4.6 LED PWM


                           The LED PWM driver is used to turn on an LED indicator light when the camera is transmitting image data. The driver
                              uses pad clock input from 6~64MHz and drives LED to be active when the OV9281 image sensor is outputting image
                                   data. The output frequency of LED PWM driver can be adjusted by LED_FREQ_DIV_CYCLE from 1k to 100kHz and the
                                 duty cycle of the output can be adjusted from 0~100% by setting the DUTY_CYCLE_REG register from 0~65535.
                           Minimum duty cycle can also be limited by register LED_DUTY_CYCLE_LOW.

                           The following parameter setting is stored in OTP memory:

                                             •  LED_DUTY_CYCLE_LOW: (16-bit) minimum duty cycle output
                                             •  LED_FREQ_DIV_CYCLE: (16-bit) LED driver output frequency divider

                           The LED PWM output is calculated as follows:

                                             •  LED_FREQ = INPUT_PAD_CLK_FREQ / LED_FREQ_DIV_CYCLE
                                             •  LED_OUTPUT_CYCLE = LED_FREQ_DIV_CYCLE × DUTY_CYCLE_REG / 65535

                      LED_PWM pin is a dedicated output pin and can only be tri-state in XSHUTDOWN mode for security. The pin will drive
                               low in software standby (sleep) and hardware standby (power down) mode.

                            figure 4-5    LED PWM output timing        ConfidentialLED_OUTPUT_CYCLE      OmnivisionINPUT_PAD_CLK


                                  LED_PWM            for

                                                                       LED_FREQ_DIV_CYCLE                                       9281_9282_DS_4_5           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 55

                                                                                                                              4-11




      table 4-8    LED PWM registers

                                                default
        address      register name         value    R/W     description

                                                                                            Bit[7:0]:  Desirable duty cycle from 0~100%
         0x3912     DUTY CYCLE REG     0x08    RW                      for LED_PWM output[15:8]
                                                                        Range: 0~65535

                                                                                            Bit[7:0]:  Desirable duty cycle from 0~100%
         0x3913     LED_PWM_REG03     0x00    RW                      for LED_PWM output[7:0]
                                                                        Range: 0~65535



      table 4-9      non-volatile memory map table (OTP)

       OTP address                  description

        0x3D0A                                   Bit[7:0]:  LED_FREQ_DIV_CYCLE[15:8]

        0x3D0B                                   Bit[7:0]:  LED_FREQ_DIV_CYCLE[7:0]

         0x3D08                                    Bit[7:0]:  LED_DUTY_CYCLE_LOW[15:8]

         0x3D09                                    Bit[7:0]:  LED_DUTY_CYCLE_LOW[7:0]


   4.7 strobe


       Strobe facilitates implementation of a flashlight. Strobe generates a pulse with a reference starting point at the time when
        the pixel array starts integration. Following a delay after the reference starting point, which is controlled by
        strobe_frame_shift_direction, strobe_frame_shift[30:0], a pulse with a width of strobe_frame_span[31:0] is generated.
      The step width of shift and span is programmable under system clock domain.        Confidential      Omnivision
      table 4-10    strobe control registers (sheet 1 of 2)                                                default     for
        address      register name         value    R/W     description

                                                                                               Bit[7]:    Shift direction
         0x3921     PWM_CTRL_21        0x00    RW                                                                                            Bit[6:0]: onlystrobe_frame_shift[30:24]

         0x3922     PWM_CTRL_22        0x00    RW           Bit[7:0]:  strobe_frame_shift[23:16]

         0x3923     PWM_CTRL_23        0x00    RW           Bit[7:0]:  strobe_frame_shift[15:8]

         0x3924     PWM_CTRL_24        0x05    RW           Bit[7:0]:  strobe_frame_shift[7:0]

         0x3925     PWM_CTRL_25        0x00    RW           Bit[7:0]:  strobe_frame_span[31:24]

         0x3926     PWM_CTRL_26        0x00    RW           Bit[7:0]:  strobe_frame_span[23:16]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 56

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 4-10    strobe control registers (sheet 2 of 2)

                                                                         default
                             address      register name         value    R/W     description

                               0x3927     PWM_CTRL_27        0x00    RW           Bit[7:0]:  strobe_frame_span[15:8]

                               0x3928     PWM_CTRL_28        0x1A    RW           Bit[7:0]:  strobe_frame_span[7:0]

                               0x3929     PWM_CTRL_29        0x01    RW           Bit[7:0]:  r_strobe_row_st[15:8]

                              0x392A     PWM_CTRL_2A        0xB4    RW           Bit[7:0]:  r_strobe_row_st[7:0]

                              0x392B     PWM_CTRL_2B        0x00    RW           Bit[7:0]:  r_strobe_cs_st[15:8]

                             0x392C     PWM_CTRL_2C        0x10    RW           Bit[7:0]:  r_strobe_cs_st[7:0]

                             0x392D     PWM_CTRL_2D        0x05    RW           Bit[7:0]:  step_onerow_man[15:8]

                              0x392E     PWM_CTRL_2E        0xF2    RW           Bit[7:0]:  step_onerow_man[7:0]

                                                                                                                                Bit[7]:   r_strobe_frm_pwen
                                                                                                                                Bit[6]:    r_strobe_frm_pwst
                                                                                                                                Bit[5]:    r_strobe_pol
                              0x392F     PWM_CTRL_2F        0x40    RW
                                                                                                                                Bit[4]:    r_strobe_step_pix
                                                                                                                                Bit[3]:   r_step_onerow_precision_man
                                                                                                                            Bit[2:0]:  r_strobe_st_opt


                    4.8 low power modes


                           The OV9281 sensor supports three low power modes:

                                             •   low frame rate streaming mode
                                             •   external trigger snapshot mode

                           table 4-11    low power mode control registers        Confidential
                                    register                  description      Omnivision
                                                    Power Control Options
                              0x4F00                     0x00:  normal mode
                                                              0x01:  low power mode            for
                                                Low Power Mode Control
                                                                                   Bit[4]:   Low frame rate streaming mode (i.e., repeating the sequence of streaming
                                                                      {0x303F} frames and then sleeping {0x302C, 0x302F} lines)           only
                               0x3030                            Bit[2]:    External trigger snapshot mode
                                                    A rising edge on FSIN pin wakes the sensor up and streams out {0x303F}
                                                                    frames
                                                                 Others:  For debug only

                              0x303F                number of active frames

                                 {0x302C, 0x320F}        number of lines of sleep period

                                                                                   Bit[1]:   MIPI power down enable during sleep period
                               0x3023
                                                                                    0:   disable for low power streaming mode





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 57

                                                                                                                              4-13




      4.8.1 low frame rate mode

        In low frame rate mode, the OV9281 sensor streams N frames, idles for M frames, and then repeats. The power
       consumption of the OV9281 sensor is close to N/(N+M) of the current in full speed streaming mode but the maximum
        integration time is limited to about tFrame - 40tRow.

      figure 4-6     low frame rate mode timing

                      N x tFrame         M x tFrame                            active   blanking





                                                                                       ...                                                                                                           ...

                                 tFrame

                                                                                                                                                                                   9281_9282_DS_4_6


      4.8.2 snapshot mode

        In snapshot mode, the OV9281 streams N frames upon request through the SCCB and then stays idle until the next
       request (see figure 4-7).

      figure 4-7     snapshot mode timing


                           snapshot request                                                     active   blanking

         SCCB

                         N x tFrame


                                                                                              ...                                                                                                           ...        Confidential
                                     tFrame      Omnivision

                                            tExposure + tFixed                                                                                                                 7750_DS_4_7            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 58

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                               4.8.3 external trigger snapshot mode

                          Upon the rising edge of FSIN pulse, the sensor wakes up from sleep mode, starts integration, reads out and sends out
                            number (set by register 0x303F) of frames. The sensor then returns back to sleep mode (see figure 4-8).

                            figure 4-8     external snapshot mode timing

                                     FSIN                                         tFSIN_High

                                                                                                                             tExp_Sof
                                    MIPI sof


                                     integration                          n                               n+1

                                     readout                  tExp_Dly                    n                                n+1




                                                                                                                                                                                                                                        9281_9282_DS_4_8


                           FSIN pulse width, tFSIN_High, should be no shorter than 5 input clock cycles. The wake up sequence takes 61396 input
                                  clock cycles, and then the pixel array is reset. The integration starts when the pixel reset finish, the interval from FSIN
                                       rising to integration, tExp_Dly, equals to 61396×tXVCLK + n×tRow, where n (≤10) is programmable. The frame start short
                                packet is sent out about 10 row periods after integration finish.

                           The reference voltage of VN2 is critical for image quality in this mode. It is recommended to have a 1µF cap on this pin
                                      to keep the voltage after the sensor goes to sleep mode between two adjacent triggers in a burst. If the sleep period is
                                  too long between bursts (e.g., more than 100ms), please discard the first triggered frame (e.g., the frame triggered by
                                  the red pulse) as shown in figure 4-9. The second frame can be triggered as early as the first frame finishes. It is
                           recommended to keep the frame rate within the burst no less than 10 fps to prevent VN2 discharging too much.

                            figure 4-9    frame triggered by red pulse diagram                                  FSINConfidential


                                                                                                                                                                                                                                  9281_9282_DS_4_9      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 59

                                                                                                                                    5-1


  5  image sensor processor digital functions

   5.1 ISP general controls


      table 5-1     ISP top registers

                                             default
        address     register name       value    R/W     description

                                                                                       Bit[7:6]:  isp_sof_sel
                                                                                          Bit[5]:    isp_eof_sel
                                                                                          Bit[4]:   bc_en
         0x5000      ISP CTRL00         0x9F    RW           Bit[3]:   wc_en
                                                                                          Bit[2]:   dpc_buf_en
                                                                                          Bit[1]:   awbg_en
                                                                                          Bit[0]:    blc_en

                                                                                          Bit[7]:   dgc_en
                                                                                       Bit[6:5]:  Reserved
                                                                                          Bit[4]:    latch_en
         0x5001      ISP CTRL 01         0x00    RW           Bit[3]:   r_size_man
                                                                                          Bit[2]:    r_pre_isp_raw_en
                                                                                          Bit[1]:   bypass_isp1
                                                                                          Bit[0]:   bypass_isp0


   5.2 manual white balance (MWB)


      The MWB provides digital gain for R, G, and B channels. Each channel gain is 12-bit. 0x400 is 1x gain.      table 5-2     manual AWB_gainConfidentialregisters      Omnivisiondefault
         address       register name           value     R/W      description

         0x3400     AWB RED GAIN          0x04     RW             Bit[3:0]: AWB red gain[11:8]            for
         0x3401     AWB RED GAIN          0x00     RW             Bit[7:0]: AWB red gain[7:0]

         0x3402     AWB GRN GAIN          0x04     RW             Bit[3:0]: AWB green gain[11:8]           only
         0x3403     AWB GRN GAIN          0x00     RW             Bit[7:0]: AWB green gain[7:0]

         0x3404     AWB BLU GAIN           0x04     RW             Bit[3:0]: AWB blue gain[11:8]

         0x3405     AWB BLU GAIN           0x00     RW             Bit[7:0]: AWB blue gain[7:0]

         0x3406     AWB MAN CTRL          0x01     RW             Bit[0]:  AWB manual control





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 60

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    5.3 manual exposure and gain control


                           table 5-3     manual exposure and gain control registers

                                                                              default
                              address       register name           value     R/W      description

                               0x3500      EXPO                   0x00     RW             Bit[3:0]:  Exposure[19:16]

                               0x3501      EXPO                   0x02     RW             Bit[7:0]:  Exposure[15:8]

                                                                                                                                     Bit[7:0]:  Exposure[7:0]
                                                                                         Low 4 bits are fraction bits
                                                                                              Minimum exposure time is
                                                                                                  1 row period. Maximum
                               0x3502      EXPO                   0x00     RW                  exposure time is frame
                                                                                                                        length - 12 row periods,
                                                                                                    where frame length is set
                                                                                                       by registers {0x380E,
                                                                                                        0x380F}

                                                                                                                                         Bit[1]:   Exposure delay option
                                                                                                             (must be 0)
                                                                                                                                   0:   Delay 1 frame
                                                                                                                                   1:   Not used
                               0x3503      MANUAL CONTROL       0x00     RW
                                                                                                                                         Bit[0]:   Exposure change delay
                                                                                                                        option (must be 0)
                                                                                                                                   0:   Delay 1 frame
                                                                                                                                   1:   Not used        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 61

                                                                                                                                    6-1


  6  system control


      System control registers include clock, reset control, and PLL configuration. Individual modules can be reset or clock
       gated by setting the appropriate registers. For system control registers, see table 7-2.

   6.1 mobile industry processor interface (MIPI)


      The OV9281 MIPI interface supports a single uni-directional clock lane and a single uni-directional data lane. The data
       lane has full support for high speed (HS) data transfer. Contact your local OmniVision FAE for more details.


      table 6-1     MIPI top control registers (sheet 1 of 7)

                                            default
        address    register name        value   R/W    description

                                                                                    Bit[7:6]:  Reserved
                                                                                       Bit[5]:   gate_sc_en
                                                                                       0:   Clock lane is free running
                                                                                       1:   Gate clock lane when there is no
                                                                               packet to transmit
                                                                                       Bit[4]:    line_sync_en
                                                                                       0:  Do not send line short packet for each
                                                                                                   line
                                                                                       1:   Send line short packet for each line
                                                                                       Bit[2]:    pclk_inv_o
                                                                                       0:   Use rising edge of mipi_pclk_o to
        0x4800     MIPI CTRL 00         0x04    RW
                                                                              generate MIPI bus to PHY
                                                                                       1:   Use falling edge of mipi_pclk_o to
                                                                              generate MIPI bus to PHY
                                                                                       Bit[1]:     firt_bit
                                                              Change clk_lane first bit
                                                                                       0:   Output 8’h55        Confidential1:   Output 8’hAA
                                                                                       Bit[0]:   LPX_select for pclk domain      Omnivision0:   Auto calculate t_lpx_p, unit: pclk2x
                                                                                     cycle
                                                                                       1:   Use lpx_p_min[7:0]            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 62

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 6-1     MIPI top control registers (sheet 2 of 7)

                                                                      default
                             address    register name        value   R/W    description

                                                                                                                        Bit[7]:    hs_prepare_sel
                                                                                                                    0:   Auto calculate T_hs_prepare,
                                                                                                                                      unit: pclk2x
                                                                                                                    1:   Use hs_prepare_min_o[7:0]
                                                                                                                        Bit[6]:    clk_prepare_sel
                                                                                                                    0:   Auto calculate T_clk_prepare,
                                                                                                                                      unit: pclk2x
                                                                                                                    1:   Use clk_prepare_min_o[7:0]
                                                                                                                        Bit[5]:    clk_post_sel
                                                                                                                    0:   Auto calculate T_clk_post,
                                                                                                                                      unit: pclk2x
                                                                                                                    1:   Use clk_post_min_o[7:0]
                                                                                                                        Bit[4]:    clk_trail_sel
                                                                                                                    0:   Auto calculate T_clk_trail, unit: pclk2x
                              0x4802     MIPI CTRL 02         0x00    RW
                                                                                                                    1:   Use clk_trail_min_o[7:0]
                                                                                                                        Bit[3]:    hs_exit_sel
                                                                                                                    0:   Auto calculate T_hs_exit, unit: pclk2x
                                                                                                                    1:   Use hs_exit_min_o[7:0]
                                                                                                                        Bit[2]:    hs_zero_sel
                                                                                                                    0:   Auto calculate T_hs_zero, unit: pclk2x
                                                                                                                    1:   Use hs_zero_min_o[7:0]
                                                                                                                        Bit[1]:    hs_trail_sel
                                                                                                                    0:   Auto calculate T_hs_trail, unit: pclk2x
                                                                                                                    1:   Use hs_trail.min_o[7:0]
                                                                                                                        Bit[0]:    clk_zero_sel
                                                                                                                    0:   Auto calculate T_clk_zero,
                                                                                                                                      unit: pclk2x
                                                                                                                    1:   Use clk_zero_min_o[7:0]

                                                                                                                     Bit[7:5]:  Reserved
                                                                                                                        Bit[4]:   pu_mark_en_o        ConfidentialBit[3]:   manu_ofset_o
                              0x4803     MIPI CTRL 03         0x10    RW                  t_perio manual offset SMIA      Omnivision    Bit[2]:   r_manual_half2one
                                                                                                           t_period half to 1 SMIA
                                                                                                                     Bit[1:0]:  Reserved                                                                                                                     Bit[7:4]:forReserved
                                                                                                                        Bit[3]:   lpda_retim_manu_o
                                                                                                                        Bit[2]:    lpda_retim_sel
                              0x4805     MIPI CTRL 05         0x00    RW                   1:   Manual           only
                                                                                                                        Bit[1]:   lpck_retim_manu_o
                                                                                                                        Bit[0]:    lpck_retim_sel
                                                                                                                    1:   Manual





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 63

                                                                                                                                    6-3




      table 6-1     MIPI top control registers (sheet 3 of 7)

                                            default
        address    register name        value   R/W    description

                                                                                    Bit[7:5]:  Reserved
                                                                                       Bit[4]:   pu_mark_en_o
                                                                Power up mark1 enable
                                                                                          test mode
                                                                                       Bit[3]:    mipi_remot_rst
        0x4806     MIPI CTRL 06         0x00    RW
                                                                                       Bit[2]:    mipi_susp
                                                                                       Bit[1]:    mipi_ul_auto_en
                                                                                       Bit[0]:    tx_lsb_first
                                                                                       0:   High bit first
                                                                                       1:   Low power transmit low bit first

                                                                                    Bit[7:4]:  Reserved
        0x4807     MIPI CTRL 07         0x03    RW         Bit[3:0]:  sw_t_lpx
                                                                                      ul_tx T_lpx

                                                                                    Bit[7:0]:  wkup_dly
        0x4808     MIPI CTRL 08         0x18    RW
                                                                 Mark1 wakeup delay/2^10

                                                            High Byte of Maximum Frame Count of Frame Sync
        0x4810    MIPI_FCNT_MAX      0xFF    RW
                                                                Short Packet

                                                  Low Byte of Maximum Frame Count of Frame Sync
        0x4811    MIPI_FCNT_MAX      0xFF    RW
                                                                Short Packet

                                                                                    Bit[7:3]:  Reserved
                                                                                       Bit[2]:    vc_sel
        0x4813     MIPI CTRL13         0x00    RW           VC or reg VC
                                                                                    Bit[1:0]: VC
                                                                                          Virtual channel of MIPI

                                                                                       Bit[7]:   Reserved
                                                                                       Bit[6]:    lpkt_dt_sel
                                                                                       0:   Use mipi_dt        0x4814     MIPI CTRL14  Confidential0x2A    RW
                                                                                       1:   Use dt_man_o as long packet data
                                                                                    Bit[5:0]:  dt_man      Omnivision
                                                                  Manual data type

                                                                                    Bit[7:2]:  Reserved
                 MIPI_HS_ZERO_                                                                                    Bit[1:0]:  hs_zero_min[9:8] for                                        0x00    RW        0x4818
                 MIN                                               High byte of minimum value of hs_zero,
                                                                                               unit: ns                                                                                    Bit[7:0]:  hs_zero_min[7:0]only
                 MIPI_HS_ZERO_                             Low byte of minimum value of hs_zero
        0x4819                         0x70    RW
                 MIN                                                  hs_zero_real =
                                                                     hs_zero_min_o + Tui*ui_hs_zero_min_o

                                                                                    Bit[7:2]:  Reserved
                  MIPI_HS_TRAIL_                                    Bit[1:0]:  hs_trail_min[9:8]
        0x481A                         0x00    RW
                 MIN                                               High byte of minimum value of hs_trail, unit:
                                                                    ns





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 64

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 6-1     MIPI top control registers (sheet 4 of 7)

                                                                      default
                             address    register name        value   R/W    description

                                                                                                                     Bit[7:0]:  hs_trail_min[7:0]
                                                                               Low byte of minimum value for hs_trail,
                                       MIPI_HS_TRAIL_
                             0x481B                       0x3C   RW                     unit: ns
                                     MIN
                                                                                                                   hs_trail_real =
                                                                                                        hs_trail_min_o + Tui*ui_hs_trail_min_o

                                                                                                                     Bit[7:2]:  Reserved
                                     MIPI_CLK_ZERO_                                   Bit[1:0]:  clk_zero_min[9:8]
                             0x481C                         0x01    RW
                                     MIN                                               High byte of minimum value for clk_zero,
                                                                                                                               unit: ns

                                                                                                                     Bit[7:0]:  clk_zero_min[7:0]
                                                                               Low byte of minimum value for clk_zero,
                                     MIPI_CLK_ZERO_
                             0x481D                       0x2C   RW                     unit: ns
                                     MIN
                                                                                                          clk_zero_real =
                                                                                                clk_zero_min_o + Tui*ui_clk_zero_min_o

                                       MIPI_CLK_                                            Bit[7:0]:  clk_prepare_max
                             0x481E                         0x5F    RW
                                 PREPARE_MAX                              Maximum value for clk_prepare, unit: ns

                                       MIPI_CLK_                                            Bit[7:0]:  clk_prepare_min
                              0x481F                         0x26    RW
                                   PREPARE_MIN                                Minimum value for clk_prepare, unit: ns

                                                                                                                     Bit[7:2]:  Reserved
                                     MIPI_CLK_POST_                                   Bit[1:0]:  clk_post_min[9:8]
                              0x4820                         0x00    RW
                                     MIN                                               High byte of minimum value for clk_post,
                                                                                                                               unit: ns

                                                                                                                     Bit[7:0]:  clk_post_min[7:0]
                                                                               Low byte of minimum value for clk_post,
                                     MIPI_CLK_POST_
                              0x4821                        0x3C   RW                     unit: ns
                                     MIN
                                                                                                           clk_post_real =
                                                                                                 clk_post_min_o + Tui*ui_clk_post_min_o        Confidential
                                                                                                                     Bit[7:2]:  Reserved      OmnivisionMIPI_CLK_TRAIL_                                   Bit[1:0]:  clk_trail_min[9:8]
                              0x4822                         0x00    RW
                                     MIN                                               High byte of minimum value for clk_trail,
                                                                                                                               unit: ns            for                                                                                                                     Bit[7:0]:  clk_trail_min[7:0]
                                                                               Low byte of minimum value for clk_trail,
                                        MIPI_CLK_TRAIL_
                              0x4823                        0x3C   RW                     unit: ns
                                     MIN           onlyclk_trail_real =
                                                                                                             clk_trail_min_o + Tui*ui_clk_trail_min_o

                                                                                                                     Bit[7:2]:  Reserved
                                                                                                                     Bit[1:0]:  lpx_p_min[9:8]
                              0x4824    MIPI_LPX_P_MIN      0x00    RW
                                                                                              High byte of minimum value for lpx_p,
                                                                                                                               unit: ns





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 65

                                                                                                                                    6-5




      table 6-1     MIPI top control registers (sheet 5 of 7)

                                            default
        address    register name        value   R/W    description

                                                                                    Bit[7:0]:  lpx_p_min[7:0]
                                                           Low byte of minimum value for lpx_p,
        0x4825    MIPI_LPX_P_MIN      0x32    RW                     unit: ns
                                                                              lpx_p_real =
                                                                      lpx_p_min_o + Tui*ui_lpx_p_min_o

                 MIPI_HS_PREPARE                                 Bit[7:0]:  hs_prepare_min
        0x4826                         0x32    RW
                 _MIN                                         Minimum value of hs_prepare, unit: ns

                 MIPI_HS_PREPARE                                 Bit[7:0]:  hs_prepare_max
        0x4827                         0x55    RW
               _MAX                                     Maximum value for hs_prepare, unit: ns

                                                                                    Bit[7:2]:  Reserved
                                                                                    Bit[1:0]:  hs_exit_min[9:8]
        0x4828     MIPI_HS_EXIT_MIN    0x00    RW
                                                                      High byte of minimum value for hs_exit,
                                                                                               unit: ns

                                                                                    Bit[7:0]:  hs_exit_min[7:0]
                                                           Low byte of minimum value for hs_exit,
        0x4829     MIPI_HS_EXIT_MIN    0x64    RW                     unit: ns
                                                                                hs_exit_real =
                                                                         hs_exit_min_o + Tui*ui_hs_exit_min_o

                 MIPI_UI_HS_ZERO
        0x482A                         0x06    RW    Minimum UI Value of hs_zero, unit: UI
                 _MIN

                  MIPI_UI_HS_TRAIL
        0x482B                         0x04    RW    Minimum UI Value of hs_trail, unit: UI
                 _MIN

                  MIPI_UI_CLK_
        0x482C                         0x00    RW    Minimum UI Value of clk_zero, unit: UI
                ZERO_MIN

                  MIPI_UI_CLK_
        0x482D                         0x00    RW    Minimum UI Value of clk_prepare, unit: UI                PREPARE_MIN Confidential

                 MIPI_UI_CLK_POST        0x482E   Omnivision0x34    RW    Minimum UI Value of clk_post, unit: UI
                 _MIN

                  MIPI_UI_CLK_
        0x482F                         0x00    RW    Minimum UI Value of clk_trail, unit: UI                  TRAIL_MIN        for

                  MIPI_UI_LPX_P_
        0x4830                         0x00    RW    Minimum UI Value of lpx_p (pclk2x domain), unit: UI
                 MIN           only
                                                                                    Bit[7:4]:  ui_hs_prepare_max
                  MIPI_UI_HS_                                Maximum UI value of hs_prepare, unit: UI
        0x4831                         0x64    RW
               PREPARE                                             Bit[3:0]:  ui_hs_prepare_min
                                                              Minimum UI value of hs_prepare, unit: UI

                  MIPI_UI_HS_EXIT_
        0x4832                         0x00    RW    Minimum UI Value of hs_exit, unit: UI
                 MIN

                 MIPI_PKT_START_                                  Bit[7:6]:  Reserved
        0x4833                         0x06    RW
                   SIZE                                                   Bit[5:0]:  mipi_pkt_start_size[5:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 66

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 6-1     MIPI top control registers (sheet 6 of 7)

                                                                      default
                             address    register name        value   R/W    description

                                       MIPI_PCLK_
                              0x4837                         0x10    RW     Period of Pclk2x, pclk_div = 1, and 1-bit Decimal
                                   PERIOD

                                                                                                                        Bit[7]:    lp_sel0
                                                                                                                    0:   Auto generate mipi_lp_dir0_o
                                                                                                                    1:   Use lp_dir_man0 to be mipi_lp_dir0_o
                                                                                                                        Bit[6]:    lp_dir_man0
                                                                                                                    0:    Input
                                                                                                                    1:   Output
                                                                                                                        Bit[5]:   lp_p0_o
                                                                                                                        Bit[4]:   lp_n0_o
                              0x4838     MIPI_LP_GPIO0       0x00    RW
                                                                                                                        Bit[3]:    lp_sel1
                                                                                                                    0:   Auto generate mipi_lp_dir1_o
                                                                                                                    1:   Use lp_dir_man1 to be mipi_lp_dir1_o
                                                                                                                        Bit[2]:    lp_dir_man1
                                                                                                                    0:    Input
                                                                                                                    1:   Output
                                                                                                                        Bit[1]:   lp_p1_o
                                                                                                                        Bit[0]:   lp_n1_o

                                                                                                                        Bit[7]:    lp_sel2
                                                                                                                    0:   Auto generate mipi_lp_dir2_o
                                                                                                                    1:   Use lp_dir_man2 to be mipi_lp_dir2_o
                                                                                                                        Bit[6]:    lp_dir_man2
                                                                                                                    0:    Input
                                                                                                                    1:   Output
                                                                                                                        Bit[5]:   lp_p2_o
                                                                                                                        Bit[4]:   lp_n2_o
                              0x4839     MIPI_LP_GPIO1       0x00    RW
                                                                                                                        Bit[3]:    lp_sel3
                                                                                                                    0:   Auto generate mipi_lp_dir3_o
                                                                                                                    1:   Use lp_dir_man3 to be mipi_lp_dir3_o
                                                                                                                        Bit[2]:    lp_dir_man3        Confidential
                                                                                                                    0:    Input      Omnivision              1:   Output
                                                                                                                        Bit[1]:   lp_p3_o
                                                                                                                        Bit[0]:   lp_n3_o                                                                                                                     Bit[7:4]:forReserved
                             0x483C    MIPI_CTRL33         0x02    RW         Bit[3:0]:  t_clk_pre
                                                                                                                      Unit: pclk2x cycle           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 67

                                                                                                                                    6-7




      table 6-1     MIPI top control registers (sheet 7 of 7)

                                            default
        address    register name        value   R/W    description

                                                                                       Bit[7]:    lp_ck_sel0
                                                                                       0:   Auto generate mipi_ck_lp_dir0_o
                                                                                       1:   Use lp_ck_dir_man0 to be
                                                                                 mipi_ck_lp_dir0_o
                                                                                       Bit[6]:    lp_dir_man0
                                                                                       0:   Input
                                                                                       1:   Output
                                                                                       Bit[5]:   lp_ck_p0_o
                                                                                       Bit[4]:   lp_ck_n0_o
        0x483D    MIPI_LP_GPIO4       0x00    RW
                                                                                       Bit[3]:    lp_ck_sel1
                                                                                       0:   Auto generate mipi_ck_lp_dir1_o
                                                                                       1:   Use lp_ck_dir_man1 to be
                                                                                 mipi_ck_lp_dir1_o
                                                                                       Bit[2]:   lp_ck_dir_man1
                                                                                       0:   Input
                                                                                       1:   Output
                                                                                       Bit[1]:   lp_ck_p1_o
                                                                                       Bit[0]:   lp_ck_n1_o

                                                                                    Bit[7:6]:  Reserved
                                                                                       Bit[5]:   slp_lp_pon_man_o
                                                                        Set for power up
                                                                                       Bit[4]:    slp_lp_pon_da
                                                                                       Bit[3]:    slp_lp_pon_ck
        0x484A    MIPI_CTRL4A         0x3F    RW
                                                                                       Bit[2]:    mipi_slp_man_st
                                                                      MIPI bus status manual control enable in
                                                                            sleep mode
                                                                                       Bit[1]:    clk_lane_state
                                                                                       Bit[0]:    data_lane_state

                                                                                    Bit[7:2]:  Reserved
                                                                                       Bit[1]:    clk_start_sel_o        Confidential
                                                                                       0:   Clock starts after SOF        0x484B    MIPI_CTRL4BOmnivision0x07    RW                   1:   Clock starts after reset
                                                                                       Bit[0]:    sof_sel_o
                                                                                       0:   Frame starts after HREF occurs
                                                                                       1:   Frame starts after SOF            for
                                                                                    Bit[7:4]:  Reserved
                                                                                       Bit[3]:   SMIA fcnt_i select
                                                                                       Bit[2]:   PRBS enable
        0x484C    MIPI_CTRL4C         0x00    RW                                                                                       Bit[1]:    hs_test_onlyonly
                                                                      MIPI high speed only test mode enable
                                                                                       Bit[0]:    Set frame count to inactive mode (keep 0)

                TEST_PATTEN_
        0x484D                        0xB6    RW     Data Lane Test Pattern Register
                DATA

        0x484E    FE_DLY              0x10    RW     Last Packet to Frame End Delay / 2

                TEST_PATTEN_CK_
        0x484F                         0x55    RW     clk_test_patten_reg
                DATA





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 68

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    6.2 digital video port (DVP)


                           The DVP provides 10-bit parallel data output.


                           table 6-2    DVP control registers

                                                                      default
                             address    register name        value   R/W    description

                                                                                                                     Bit[1:0]: VSYNC output select
                                                                                                         00:  eof_o
                              0x4701    VSYNCOUT_SEL      0x00    RW                 01:  Output gpo_vsync
                                                                                                         10:  Output hsync_o
                                                                                                               11:  eof_o

                                                                                                                     Bit[7:0]:  vsync_rise_lnt[15:8]
                              0x4702    VSYNC_RISE_LNT     0x00    RW                 Line counter that controls rising position of
                                                                      VSYNC

                                                                                                                     Bit[7:0]:  vsync_rise_lnt[7:0]
                              0x4703    VSYNC_RISE_LNT     0x02    RW                 Line counter that controls rising position of
                                                                      VSYNC

                                                                                                                     Bit[7:0]:  vsync_fall_lnt[15:8]
                              0x4704    VSYNC_FALL_LNT     0x00    RW                 Line counter that controls falling position of
                                                                      VSYNC

                                                                                                                     Bit[7:0]:  vsync_fall_lnt[7:0]
                              0x4705    VSYNC_FALL_LNT     0x06    RW                 Line counter that controls falling position of
                                                                      VSYNC

                                                                                                                     Bit[7:0]:  vsync_chg_pcnt[15:8]
                              0x4706    VSYNC_CHG_PCNT   0x00    RW           VSYNC change position indicated by pixel
                                                                                                                position

                                                                                                                     Bit[7:0]:  vsync_chg_pcnt[7:0]        Confidential
                              0x4707    VSYNC_CHG_PCNT   0x10    RW           VSYNC change position indicated by pixel
                                                                                                                position      Omnivision

                                                                                                                         Bit[7]:    Clock DDR mode enable
                                                                                                                         Bit[6]:   hts_man_en                                                                                                                         Bit[5]:forVSYNC gate enable
                                                                                                                         Bit[4]:   HREF gate enable
                              0x4708    POLARITY_CTRL      0x09    RW
                                                                                                                         Bit[3]:    r_fo_nofrst
                                                                                                                         Bit[2]:   HREF polarity           onlyBit[1]:   VSYNC polarity
                                                                                                                         Bit[0]:   PCLK polarity

                              0x4709    BIT_TEST_ORDER     0x00    RW         Bit[6:4]:  Data bit swap

                                                                                                                         Bit[1]:    first_lv_sel
                             0x470C    R_READ_CTRL        0x81    RW
                                                                                                                         Bit[0]:    r_lpcnt_free

                                                                                                                         Bit[4]:    href_sel
                              0x470F    BYP_SEL             0x00    RW
                                                                                                                     Bit[3:0]:  bypass_sel





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 69

                                                                                                                                    7-1


  7  register tables


      The following tables provide descriptions of the device control registers contained in the OV9281. For all register
        enable/disable bits, ENABLE = 1 and DISABLE = 0. The device slave addresses are 0xC0 for write and 0xC1 for read.

   7.1 system control [0x0100 ~ 0x010A, 0x3000 ~ 0x301F ~ 0x303F]


      table 7-1     system control registers (sheet 1 of 8)

                                                default
        address     register name          value    R/W     description

                                                                                           Bit[7:1]:  Not used
                                                                                              Bit[0]:   Mode select
         0x0100    SC_MODE_SELECT     0x00    RW
                                                                                             0:   software_standby
                                                                                             1:   Streaming

        0x0101~
                RSVD                 –         –       Reserved
         0x0102

                                                                                           Bit[7:1]:  Not used
                SC_SOFTWARE_                                          Bit[0]:    software_reset
         0x0103                            0x00    RW
                RESET                                                           0:    Off
                                                                                             1:  On

        0x0104~
                RSVD                 –         –       Reserved
         0x0105

                                                                                           Bit[7:1]:  Not used
                                                                                              Bit[0]:    fast_standby
                 SC_FAST_STANDBY                                           0:   Frame completes before mode
         0x0106                            0x01    RW
                 _CTRL                                                            entry
                                                                                             1:   Frame may be truncated before
                                                                  mode entry        Confidential
                   CCI ADDRESS
                                           0x20    RW     CCI Slave 20         0x0107                CONTROLOmnivision20

                   CCI ADDRESS
         0x0108                          0x6C    RW     CCI Slave 6C
                CONTROL 6C            for
                   CCI ADDRESS
         0x0109                          0xC0    RW     CCI Slave C0
                CONTROL C0                   CCI ADDRESS      only
        0x010A                          0x7C    RW     CCI Slave 7C
                CONTROL 7C

         0x3000    RSVD                 –         –       Reserved

                                                                                           Bit[7:6]:  pd_data_en
         0x3001     SC_CTRL_01           0x02    RW
                                                                                           Bit[5:0]:  pd_pk

        0x3002~
                RSVD                 –         –       Reserved
         0x3003





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 70

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-1     system control registers (sheet 2 of 8)

                                                                         default
                             address     register name          value    R/W     description

                                                                                                                           Bit[7:2]:  Reserved
                               0x3004     SC_CTRL_04           0x00    RW
                                                                                                                            Bit[1:0]:  io_pad_out_en[17:16]

                               0x3005     SC_CTRL_05           0x00    RW           Bit[7:0]:  io_pad_out_en[15:8]

                               0x3006     SC_CTRL_06           0x00    RW           Bit[7:0]:  io_pad_out_en[7:0]

                                                                                                                           Bit[7:2]:  Reserved
                               0x3007     SC_CTRL_07           0x00    RW
                                                                                                                            Bit[1:0]:  io_pad_out[17:16]

                               0x3008     SC_CTRL_08           0x00    RW           Bit[7:0]:  io_pad_out[15:8]

                               0x3009     SC_CTRL_09           0x00    RW           Bit[7:0]:  io_pad_out[7:0]

                              0x300A     SC_CHIP_ID_HIGH      0x92     R       Chip ID High Byte

                              0x300B     SC_CHIP_ID_LOW      0x81     R       Chip ID Low Byte

                             0x300C    SC_CTRL_0C          –       R        Revision ID

                                                                                                                               Bit[7]:    gclk_embline
                                                                                                                               Bit[6]:   gclk_dpcm
                                                                                                                               Bit[5]:    gclk_testmode
                                                                                                                               Bit[4]:    gclk_smia
                             0x300D    SC_CTRL_0D          0x00    RW
                                                                                                                               Bit[3]:    gclk_otp
                                                                                                                               Bit[2]:   Reserved
                                                                                                                               Bit[1]:    gclk_mipi
                                                                                                                               Bit[0]:    gclk_dvp

                                                                                                                               Bit[7]:    gclk_vfifo
                                                                                                                               Bit[6]:   Reserved
                                                                                                                               Bit[5]:    gclk_isp
                                                                                                                               Bit[4]:    gclk_blc
                              0x300E     SC_CTRL_0E           0x00    RW
                                                                                                                               Bit[3]:   Reserved        ConfidentialBit[2]:    gclk_aec
                                                                                                                               Bit[1]:    gclk_stb      Omnivision                                                                                                                               Bit[0]:    gclk_fc

                                                                                                                               Bit[7]:   Reserved
                              0x300F     SC_CTRL_0F          0xF0    RW           Bit[6]:   daclk_gs_en            for
                                                                                                                           Bit[5:0]:  Reserved

                                                                                                                               Bit[7]:   scale_div_man_en
                                                                                                                               Bit[6]:    daclk_en           only
                                                                                                                            Bit[5:4]:  daclk_sel
                                                                                                              00:  /1
                               0x3010     SC_CTRL_10          0xC1    RW                  01:  /2
                                                                                                              10:  /4
                                                                                                                    11:   /1
                                                                                                                               Bit[3]:   Reserved
                                                                                                                           Bit[2:0]:  pll_scale_div





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 71

                                                                                                                                    7-3




      table 7-1     system control registers (sheet 3 of 8)

                                                default
        address     register name          value    R/W     description

                                                                                              Bit[7]:   hstx_open_term
                                                                                           Bit[6:5]:  d1_skew
                                                                                              Bit[4]:    pllclk_out_enable
         0x3011     SC_CTRL_11           0x08    RW
                                                                                              Bit[3]:   mipi_pad
                                                                                           Bit[2:0]:  pgm_vcm
                                                                           High speed common mode voltage

                                                                                           Bit[7:4]:  sel_drv
         0x3012     SC_MIPI_PHY0         0x70    RW           Bit[3:2]:  ck_skew
                                                                                           Bit[1:0]:  d0_skew

                                                                                              Bit[7]:    lp_sr
                                                                                              Bit[6]:   pgm_bp_hs_en_lat
                                                                                           Bit[5:4]:  pgm_lptx
         0x3013     SC_MIPI_PHY1         0x10    RW                   Driving strength control of low speed
                                                                                          transmitter
                                                                                           Bit[3:2]:  r_iref
                                                                                           Bit[1:0]:  bitsel

                                                                                           Bit[7:6]:  mipi_ck_skew_o
                                                                                              Bit[5]:    mipi_phy_rst_o
                                                                                              Bit[4]:   r_phy_pd_mipi
                                                                                             1:   Power down PHY HS TX
                                                                                              Bit[3]:    r_phy_pd_lprx
                                                                                             1:   Power down PHY LP RX
                                                                           module
                                                                                              Bit[2]:    mipi_en
                                                                                             0:  DVP enable
         0x3014     SC_MIPI_SC_CTRL0    0x04    RW                    1:   MIPI enable
                                                                                              Bit[1]:    mipi_susp_reg
                                                                           MIPI system suspend register
                                                                                             1:   Suspend        Confidential                                                                                              Bit[0]:    lane_dis_op
                                                                                             0:   Use mipi_release1/2 and      Omnivision                                                                                        lane_disable1/2 to disable two
                                                                                     data lanes
                                                                                             1:   Use lane_disable1/2 to disable                                                                               two data lanesfor

                                                                                           Bit[7:0]:  MIPI ULPS resume mark1 detect
         0x3015     SC_MIPI_SC_CTRL1    0x10    RW
                                                                                     length           only
                                                                                              Bit[7]:    sclk_fc
                                                                                              Bit[6]:    sclk_stb
                                                                                              Bit[5]:    sclk_aec
                                                                                              Bit[4]:    sclk_tc
         0x3016     SC_CLKRST0          0xF0    RW
                                                                                              Bit[3]:    rst_fc
                                                                                              Bit[2]:    rst_stb
                                                                                              Bit[1]:    rst_aec
                                                                                              Bit[0]:    rst_tc





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 72

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-1     system control registers (sheet 4 of 8)

                                                                         default
                             address     register name          value    R/W     description

                                                                                                                               Bit[7]:    sclk_blc
                                                                                                                                Bit[6]:    sclk_isp
                                                                                                                               Bit[5]:    sclk_psv_ctrl
                                                                                                                               Bit[4]:    sclk_vfifo
                               0x3017     SC_CLKRST1          0xF0    RW
                                                                                                                               Bit[3]:    rst_blc
                                                                                                                               Bit[2]:    rst_isp
                                                                                                                               Bit[1]:    rst_psv_ctrl
                                                                                                                               Bit[0]:     rst_vfifo

                                                                                                                               Bit[7]:    pclk_dvp
                                                                                                                               Bit[6]:    sclk_mipi
                                                                                                                               Bit[5]:    sclk_ac
                                                                                                                               Bit[4]:    sclk_otp
                               0x3018     SC_CLKRST2          0xF0    RW
                                                                                                                               Bit[3]:    rst_dvp
                                                                                                                               Bit[2]:    rst_mipi
                                                                                                                               Bit[1]:    rst_ac
                                                                                                                               Bit[0]:    rst_otp

                                                                                                                               Bit[7]:    sclk_smia
                                                                                                                               Bit[6]:    sclk_testmode
                                                                                                                               Bit[5]:   sclk_dpcm
                                                                                                                               Bit[4]:    sclk_embline
                               0x3019     SC_CLKRST3          0xF0    RW
                                                                                                                               Bit[3]:    rst_smia
                                                                                                                               Bit[2]:    rst_testmode
                                                                                                                               Bit[1]:   rst_dpcm
                                                                                                                               Bit[0]:    rst_embline

                                                                                                                               Bit[7]:    sdclk_sd
                                                                                                                               Bit[6]:    padclk_mipi_sc
                                                                                                                               Bit[5]:    pclk_vfifo
                                                                                                                               Bit[4]:    pclk_mipi
                              0x301A     SC_CLKRST4          0xF0    RW
                                                                                                                                Bit[3]:    rst_sd        Confidential                                                                                                                               Bit[2]:    rst_mipi_s
                                                                                                                               Bit[1]:    rst_ulpm      Omnivision                                                                                                                               Bit[0]:    rst_srb

                                                                                                                               Bit[7]:    sclk_src
                                                                                                                                Bit[6]:    sclk_cvdn            for
                                                                                                                               Bit[5]:    sclk_asram_tst
                                                                                                                               Bit[4]:    sclk_snr_sync
                              0x301B     SC_CLKRST5          0xF0    RW
                                                                                                                               Bit[3]:    rst_src           onlyBit[2]:    rst_cvdn
                                                                                                                               Bit[1]:    rst_asram_tst
                                                                                                                                Bit[0]:    rst_snr_sync





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 73

                                                                                                                                    7-5




      table 7-1     system control registers (sheet 5 of 8)

                                                default
        address     register name          value    R/W     description

                                                                                              Bit[7]:    sclk_bist
                                                                                              Bit[6]:    sclk_srb
                                                                                              Bit[5]:    sclk_grp
                                                                                              Bit[4]:    sclk_ulpm
        0x301C    SC_CLKRST6          0xF2    RW
                                                                                              Bit[3]:    rst_bist
                                                                                              Bit[2]:    rst_srb
                                                                                              Bit[1]:    rst_grp
                                                                                              Bit[0]:    rst_ulpm

                                                                                              Bit[7]:   frex_mask_aec
                                                                                              Bit[6]:    frex_mask_blc
                                                                                              Bit[5]:    frex_mask_isp
                                                                                              Bit[4]:   frex_mask_dvp
        0x301D    SC_FREX_RST         0x00    RW
                                                                                              Bit[3]:    frex_mask_mipi
                                                                                              Bit[2]:    frex_mask_vfifo
                                                                                              Bit[1]:   Reserved
                                                                                              Bit[0]:   frex_mask_mipi_phy

                                                                                           Bit[7:5]:  sdiv
                                                                                       Divider for sigma delta (sdclk)
                                                                                              Bit[4]:   Reserved
                                                                                              Bit[3]:    pclk_sel
                                                                                             0:    pll_pclk
        0x301E     SC_CTRL_1E           0x03    RW
                                                                                             1:   pll_pclk_d2
                                                                                           Bit[2:1]:  Reserved
                                                                                              Bit[0]:    sclk2x_source_sel
                                                                                             0:    pll_sclk
                                                                                             1:   pll_sclk_d2

                                                                                           Bit[7:6]:  mipi_data_skew_o
                                                                                              Bit[5]:    mipi_clk_lane_ctrl
                                                                                             0:   Clock lane hold LP00 when        Confidential                                                                                  pd_mipi
                                                                                             1:   Clock lane is high-z when      Omnivision                                                                                  pd_mipi
                                                                                              Bit[4]:    mipi_ctr_en
                                                                                             0:   Disable the function        0x301F     SC_CLOCK_SEL        0x03    RW                    1:   Enable MIPIforremote reset and
                                                                            suspend control sc
                                                                                              Bit[3]:    mipi_rst_sel
                                                                                             0:   MIPI remote reset all registers           only1:   MIPI remote reset all digital
                                                                           module
                                                                                              Bit[2]:    gpio_pclk_en
                                                                                              Bit[1]:    frex_ef_sel
                                                                                              Bit[0]:    cen_global_o





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 74

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-1     system control registers (sheet 6 of 8)

                                                                         default
                             address     register name          value    R/W     description

                                                                                                                               Bit[7]:   gclk_pwm
                                                                                                                               Bit[6]:    gclk_fmt
                                                                                                                               Bit[5]:    gclk_strobe
                                                                                                                               Bit[4]:   Reserved
                               0x3020     SC_CTRL_20           0x00    RW
                                                                                                                               Bit[3]:    gclk_ulpm
                                                                                                                               Bit[2]:   Reserved
                                                                                                                               Bit[1]:    gclk_srb
                                                                                                                               Bit[0]:    gclk_bist

                                                                                                                               Bit[7]:    gclk_sync
                                                                                                                               Bit[6]:    gclk_asram_tst
                                                                                                                               Bit[5]:    gclk_cvdn
                               0x3021     SC_CTRL_21           0x00    RW           Bit[4]:    gclk_src
                                                                                                                               Bit[3]:    gclk_mipi
                                                                                                                               Bit[2]:    gclk_vfifo
                                                                                                                           Bit[1:0]:  Reserved

                                                                                                                            Bit[7:4]:  mipi_bit_sel
                                                                                                      0100:    8-bit mode
                                                                                                      0101:    10-bit mode
                                                                                                         0110:    12-bit mode
                                                                                                            Others: Reserved
                               0x3022     SC_MISC_CTRL        0x01    RW
                                                                                                                               Bit[3]:   mipi_lvds_mode_o
                                                                                                                               Bit[2]:    Clock lane disable 1
                                                                                                                               Bit[1]:    Clock lane disable 2
                                                                                                                               Bit[0]:    pd_mipi_rst_sync
                                                                                                      pd_mipi enable when rst_sync

                                                                                                                               Bit[7]:   Reserved
                                                                                                                                Bit[6]:   phy_pd_mipi_pwdn_dis
                                                                                                                               Bit[5]:   phy_pd_lrpx_pwdn_dis
                                                                                                                               Bit[4]:    r_stb_rst_dis_o        Confidential                                                                                                                          0:   Reset all blocks in software
                                                                                                           standby mode      Omnivision0x3023     SC_CTRL_23           0x07    RW                                                                                                                          1:   TC, sensor_control, ISP are
                                                                                                                                reset, others are not
                                                                                                                               Bit[3]:   pd_ana_dis            forBit[2]:   Reserved
                                                                                                                                Bit[1]:    phy_pd_mipi_slppd_dis
                                                                                                                               Bit[0]:    phy_pd_lprx_slppd_dis           onlyBit[7:1]:  Reserved
                               0x3024     SC_CTRL_24           0x00    RW
                                                                                                                               Bit[0]:    pd_mipi_auto

                                                                                                                           Bit[7:2]:  Reserved
                               0x3025     SC_GP_IO_SEL0        0x00    RW
                                                                                                                            Bit[1:0]:  io_pad_sel_o[17:16]

                               0x3026     SC_GP_IO_SEL1        0x00    RW           Bit[7:0]:  io_pad_sel_o[15:8]

                               0x3027     SC_GP_IO_SEL2        0x00    RW           Bit[7:0]:  io_pad_sel_o[7:0]

                               0x3028     SC_GP_IO_IN0         –       R             Bit[7:0]:  io_pad_i[23:16]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 75

                                                                                                                                    7-7




      table 7-1     system control registers (sheet 7 of 8)

                                                default
        address     register name          value    R/W     description

         0x3029     SC_GP_IO_IN1         –       R             Bit[7:0]:  io_pad_i[15:8]

        0x302A     SC_GP_IO_IN2         –       R             Bit[7:0]:  io_pad_i[7:0]

        0x302B     SC_CTRL_2B          0xE0    RW           Bit[7:0]:  sccb_id[7:0]

        0x302C     SC_LP_CTRL0          0x01    RW           Bit[7:0]:  r_sleep_period[31:24]

        0x302D     SC_LP_CTRL1          0x00    RW           Bit[7:0]:  r_sleep_period[23:16]

        0x302E     SC_LP_CTRL2          0x00    RW           Bit[7:0]:  r_sleep_period[15:8]

        0x302F     SC_LP_CTRL3          0x00    RW           Bit[7:0]:  r_sleep_period[7:0]

                                                                                              Bit[7]:    r_auto_sleep_en
                                                                                              Bit[6]:    r_gpio_sel
                                                                                             1:   Sleep can be read by GPIO
                                                                                       (Y9)
         0x3030     SC_LP_CTRL4          0x10    RW           Bit[5]:   r_wake_up_pol
                                                                                              Bit[4]:    r_aslp_repeat
                                                                                              Bit[3]:   r_als_wake_up_en
                                                                                              Bit[2]:   r_fsin_wake_up_en
                                                                                           Bit[1:0]:  Reserved

                                                                                           Bit[7:2]:  Reserved
         0x3031     IO_Y_OEN_SLEEP0     0x03    RW
                                                                                           Bit[1:0]:  io_y_oen_sleep[17:16]

         0x3032     IO_Y_OEN_SLEEP1     0xFF    RW           Bit[7:0]:  io_y_oen_sleep[15:8]

         0x3033     IO_Y_OEN_SLEEP2     0xFF    RW           Bit[7:0]:  io_y_oen_sleep[7:0]

                                                                                           Bit[7:2]:  Reserved
         0x3034     IO_Y_OEN_PWDN0     0x03    RW
                                                                                           Bit[1:0]:  io_y_oen_pwdn[17:16]

         0x3035     IO_Y_OEN_PWDN1     0xFF    RW           Bit[7:0]:  io_y_oen_pwdn[15:8]        Confidential

         0x3036     IO_Y_OEN_PWDN2     0xFF    RW           Bit[7:0]:  io_y_oen_pwdn[7:0]      Omnivision
                                                                                              Bit[7]:   Reserved
                                                                                              Bit[6]:    sclk_strobe
                                                                                              Bit[5]:    sclk_fmt            for
                                                                                              Bit[4]:   sclk_pwm
         0x3037     SC_CTRL_37           0xF0    RW
                                                                                              Bit[3]:    rst_grp
                                                                                              Bit[2]:    rst_strobe                                                                                              Bit[1]: onlyrst_fmt
                                                                                              Bit[0]:   rst_pwm

                                                                                           Bit[7:5]:  Reserved
                                                                                              Bit[4]:   padclk_ulpm
                                                                                              Bit[3]:   Reserved
         0x3038     SC_CTRL_38           0x58    RW
                                                                                              Bit[2]:    frex_mask_strobe
                                                                                              Bit[1]:    frex_mask_fmt
                                                                                              Bit[0]:   frex_mask_pwm





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 76

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-1     system control registers (sheet 8 of 8)

                                                                         default
                             address     register name          value    R/W     description

                                                                                                                           Bit[7:5]:  mipi_lane_num
                                                                                                         000: One-lane mode
                                                                                                         001: Two-lane mode
                                                                                                                               Bit[4]:    mipi_en
                                                                                                                          0:  DVP enable
                               0x3039     SC_CTRL_39           0x32    RW                    1:   MIPI enable
                                                                                                                           Bit[3:2]:  r_phy_pd_mipi
                                                                                                                                Bit[1]:    phy_rst option
                                                                                                                          1:   Reset PHY when rst_sync
                                                                                                                               Bit[0]:    lane_dis_option
                                                                                                                          1:   Disable lanes when pd_mipi

                              0x303A     SC_CTRL_3A           0x00    RW           Bit[7:0]:  MIPI lane disable

                                                                                                                           Bit[7:2]:  Reserved
                              0x303B     SC_CTRL_3B           0x00    RW           Bit[1]:   sccb_pgm_id_en
                                                                                                                               Bit[0]:   sccb_id2_nack_en

                                                                                                                           Bit[7:4]:  Reserved
                             0x303C    SC_CTRL_3C          0x00    RW
                                                                                                                            Bit[3:0]:  r_ppump_div

                                                                                                                            Bit[7:4]:  r_npump2_div
                             0x303D    SC_CTRL_3D          0x02    RW
                                                                                                                            Bit[3:0]:  r_npump1_div

                                                                                                                           Bit[7:6]:  Reserved
                              0x303E     SC_CTRL_3E           0x07    RW
                                                                                                                            Bit[5:0]:  r_pump_ctrl

                              0x303F     SC_CTRL_3F           0x03    RW           Bit[7:0]:  r_frame_on_num


                    7.2 PLL control [0x0300 ~ 0x0319]        Confidential      Omnivision                           table 7-2    PLL control registers (sheet 1 of 3)

                                                                            default                             address     register name             value    R/W fordescription

                                                                                                                                Bit[7:3]:  Reserved
                               0x0300     PLL_CTRL_00             0x01    RW
                                                                                                                               Bit[2:0]:  pll1_pre_div           only
                                                                                                                                Bit[7:2]:  Reserved
                               0x0301     PLL_CTRL_01             0x00    RW
                                                                                                                               Bit[1:0]:  pll1_multiplier[9:8]

                               0x0302     PLL_CTRL_02             0x32    RW          Bit[7:0]:  pll1_multiplier[7:0]

                                                                                                                                Bit[7:4]:  Reserved
                               0x0303     PLL_CTRL_03             0x00    RW
                                                                                                                               Bit[3:0]:  pll1_m_div

                                                                                                                                Bit[7:2]:  Reserved
                               0x0304     PLL_CTRL_04             0x03    RW
                                                                                                                               Bit[1:0]:  pll1_mipi_div





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 77

                                                                                                                                    7-9




      table 7-2    PLL control registers (sheet 2 of 3)

                                                   default
        address     register name             value    R/W     description

                                                                                               Bit[7:2]:  Reserved
         0x0305     PLL_CTRL_05             0x02    RW
                                                                                               Bit[1:0]:  pll1_pre_sys_div

                                                                                               Bit[7:1]:  Reserved
         0x0306     PLL_CTRL_06             0x01    RW
                                                                                                  Bit[0]:    pll1_sys_div

                                                                                               Bit[7:1]:  Reserved
         0x0307     PLL_CTRL_07             0x00    RW
                                                                                                  Bit[0]:    pll1_div_rst_sync

                                                                                               Bit[7:1]:  Reserved
         0x0308     PLL_CTRL_08             0x00    RW
                                                                                                  Bit[0]:    pll1_bypass

                                                                                               Bit[7:1]:  Reserved
         0x0309     PLL_CTRL_09             0x01    RW
                                                                                               Bit[2:0]:  pll1_cp

                                                                                               Bit[7:1]:  Reserved
        0x030A     PLL_CTRL_0A             0x00    RW
                                                                                                  Bit[0]:    pll1_pre_div0

                                                                                               Bit[7:3]:  Reserved
        0x030B     PLL_CTRL_0B             0x04    RW
                                                                                               Bit[2:0]:  pll2_pre_div

                                                                                               Bit[7:2]:  Reserved
        0x030C     PLL_CTRL_0C             0x00    RW
                                                                                               Bit[1:0]:  pll2_multiplier[9:8]

        0x030D     PLL_CTRL_0D             0x50    RW          Bit[7:0]:  pll2_multiplier[7:0]

                                                                                               Bit[7:3]:  Reserved
        0x030E     PLL_CTRL_0E             0x02    RW
                                                                                               Bit[2:0]:  pll2_sys_div

                                                                                               Bit[7:4]:  Reserved
        0x030F     PLL_CTRL_0F             0x03    RW
                                                                                               Bit[3:0]:  pll2_pre_sys_div

                                                                                               Bit[7:3]:  Reserved
         0x0310     PLL_CTRL_10             0x01    RW
                                                                                               Bit[2:0]:  pll2_cp        Confidential
                                                                                               Bit[7:1]:  Reserved
         0x0311     PLL_CTRL_11             0x00    RW      OmnivisionBit[0]:    pll2_bypass

                                                                                               Bit[7:4]:  Reserved
         0x0312     PLL_CTRL_12             0x07    RW
                                                                                               Bit[3:0]:  pll2_ana_div            for
                                                                                               Bit[7:4]:  Reserved
         0x0313     PLL_CTRL_13             0x01    RW
                                                                                               Bit[3:0]:  pll2_adc_div

                                                                                               Bit[7:1]:  Reserved         0x0314     PLL_CTRL_14             0x00    RW  only
                                                                                                  Bit[0]:    pll2_pre_div0

                                                                                               Bit[7:1]:  Reserved
         0x0315     PLL_CTRL_15             0x00    RW
                                                                                                  Bit[0]:    pll2_div_rst_sync

        0x0316~
                RSVD                    –         –       Reserved
         0x0317

                                                                                               Bit[7:1]:  Reserved
         0x0318     PLL_CTRL_18             0x00    RW
                                                                                                  Bit[0]:    pll1_rst_o





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 78

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-2    PLL control registers (sheet 3 of 3)

                                                                            default
                             address     register name             value    R/W     description

                                                                                                                                Bit[7:1]:  Reserved
                               0x0319     PLL_CTRL_19             0x00    RW
                                                                                                                                   Bit[0]:    pll2_rst_o


                    7.3 SCCB and group hold control [0x3100 ~ 0x3107, 0x31FF ~ 0x320F]


                           table 7-3    SCCB and group hold registers (sheet 1 of 2)

                                                                      default
                             address     register name       value    R/W     description

                              0x3100~
                                DEBUG CTRL        –         –       Debug Control
                               0x3105

                                                                                                                            Bit[7]:   sclk2x_o_en
                                                                                                                            Bit[6]:    sclk_o_en
                                                                                                                         Bit[5:3]:  arb_ctrl[2:0]
                                                                                                                            Bit[2]:   pad_clk_sw
                               0x3106     SB_SRB_CTRL0      0x08    RW           Bit[1:0]:  System clock select
                                                                                                           00:  pll_sclk
                                                                                                            01:  pll_sclk_d2
                                                                                                            10:  pll_sclk_d4
                                                                                                                  11:   pll_sclk

                                                                                                                         Bit[7:6]:  Reserved
                                                                                                                            Bit[5]:   disable_auto_wake
                                                                                                                            Bit[4]:    pd_mipi_dis_aslp
                               0x3107     SB_SRB_CTRL1      0x00    RW           Bit[3]:    pumpclk_cutoff_byp
                                                                                                                            Bit[2]:    pclk_cutoff_byp        Confidential
                                                                                                                            Bit[1]:    clk_cutoff_byp
                                                                                                                            Bit[0]:     clk_ctrl      Omnivision
                                                                                                                         Bit[7:1]:  Debug control
                                                                                                                            Bit[0]:   SCCB slave select
                                                                                                                       0:   Select SCCB slave which requires                              0x31FF    SB_SWITCH         0x01    RW for
                                                                                XVCLK
                                                                                                                       1:   Select SCCB slave which does
                                                                                                                 not require XVCLK           only
                                                                                                            Start Address of Group 0 Buffer
                               0x3200    GROUP ADR0        0x00    RW
                                                                                                Actual Start Address is {0x3200[3:0], 4'h0}

                                                                                                            Start Address of Group 1 Buffer
                               0x3201    GROUP ADR1        0x04    RW
                                                                                                Actual Start Address Is {0x3201[3:0], 4'h0}

                              0x3202~
                                 RSVD               –         –        Reserved
                               0x3203

                               0x3204    GROUP LEN0        –       R        Length of Group0





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 79

                                                                                                                              7-11




      table 7-3    SCCB and group hold registers (sheet 2 of 2)

                                             default
        address     register name       value    R/W     description

         0x3205    GROUP LEN1        –       R        Length of Group1

        0x3206~
                RSVD               –         –        Reserved
         0x3207

                                                                                        Bit[7:4]:  group_ctrl
                                                                           0000:   Group hold start
                                                                           0001:   Group hold end
                                                                             0110:   Group launch at line blanking
                                                                           1010:   Group launch at vertical
                                                                                         blanking
        0x3208    GROUP ACCESS     –      W
                                                                                1110:   Group launch immediately
                                                                                Others: Debug control
                                                                                        Bit[3:0]:  Group ID
                                                                           0000:   Group bank 0
                                                                           0001:   Group bank 1
                                                                                Others: Debug control

         0x3209    GROUP0 PERIOD     0x00    RW     Number of Frames to Stay in Group0

        0x320A    GROUP1 PERIOD     0x00    RW     Number of Frames to Stay in Group1

                                                                                        Bit[7:6]:  Debug control
                                                                                           Bit[5]:    grp0_start_opt
                                                                                           Bit[4]:    frame_cnt_trig
        0x320B    GRP_SW_CTRL      0x01    RW
                                                                                           Bit[3]:    group_switch_repeat
                                                                                           Bit[2]:   Group switch enable
                                                                                        Bit[1:0]:  Second group select

                                                                                        Bit[7:5]:  Debug control
        0x320C    SRAM TEST         0x0F    RW           Bit[4]:   Group hold SRAM test enable
                                                                                        Bit[3:0]:  Group hold SRAM RM[3:0]        0x320D    GRP_ACT  Confidential–       R         Active Group Indicator        0x320E    FM_CNT_GRP0Omnivision–       R       Group0 Frame Count

        0x320F    FM_CNT_GRP1      –       R       Group 1 Frame Count            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 80

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    7.4 manual AWB_gain control [0x3400 ~ 0x3406]


                           table 7-4     manual AWB_gain registers

                                                                       default
                             address     register name       value    R/W     description

                                                                                                                         Bit[7:4]:  Debug control
                               0x3400    AWB RED GAIN      0x04    RW
                                                                                                                         Bit[3:0]: AWB red gain[11:8]

                               0x3401    AWB RED GAIN      0x00    RW           Bit[7:0]: AWB red gain[7:0]

                                                                                                                         Bit[7:4]:  Debug control
                               0x3402    AWB GRN GAIN      0x04    RW
                                                                                                                         Bit[3:0]: AWB green gain[11:8]

                               0x3403    AWB GRN GAIN      0x00    RW           Bit[7:0]: AWB green gain[7:0]

                                                                                                                         Bit[7:4]:  Debug control
                               0x3404    AWB BLU GAIN       0x04    RW
                                                                                                                         Bit[3:0]: AWB blue gain[11:8]

                               0x3405    AWB BLU GAIN       0x00    RW           Bit[7:0]: AWB blue gain[7:0]

                                                                                                                         Bit[7:1]:  Debug control
                               0x3406    AWB MAN CTRL      0x01    RW
                                                                                                                             Bit[0]:  AWB manual control


                    7.5 manual AEC/AGC [0x3500 ~ 0x3512, 0x3519 ~ 0x351D]


                           table 7-5     manual AEC/AGC registers (sheet 1 of 3)

                                                                     default
                             address    register name        value   R/W    description        Confidential
                                                                                                                    Bit[7:4]:  Not used
                              0x3500    EXPO               0x00    RW
                                                                                                                    Bit[3:0]:  Exposure[19:16]      Omnivision

                               0x3501    EXPO               0x02    RW         Bit[7:0]:  Exposure[15:8]

                                                                                                                    Bit[7:0]:  Exposure[7:0]            for
                                                                              Low 4 bits are fraction bits
                                                                                   Minimum exposure time is 1 row period.
                              0x3502    EXPO               0x00    RW
                                                                             Maximum exposure time is frame length - 12           onlyrow periods, where frame length is set by
                                                                                                                registers {0x380E, 0x380F}





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 81

                                                                                                                              7-13




      table 7-5     manual AEC/AGC registers (sheet 2 of 3)

                                            default
        address    register name        value   R/W    description

                                                                                      Bit[7]:   Not used
                                                                                      Bit[6]:    Digital fraction gain delay option
                                                                                      0:   Delay 1 frame
                                                                                      1:  Do not delay 1 frame
                                                                                      Bit[5]:   Gain change delay option
                                                                                      0:   Delay 1 frame
                                                                                      1:  Do not delay 1 frame
                                                                                      Bit[4]:   Gain delay option
                                                                                      0:   Delay 1 frame
                                                                                      1:  Do not delay 1 frame
        0x3503    AEC MANUAL        0x00    RW
                                                                                      Bit[3]:   gain_prec16_en
                                                                                      Bit[2]:   Gain manual as sensor gain
                                                                                      0:    Input gain as real gain format
                                                                                      1:    Input gain as sensor gain format
                                                                                      Bit[1]:   Exposure delay option (must be 0)
                                                                                      0:   Delay 1 frame
                                                                                      1:   Not used
                                                                                      Bit[0]:   Exposure change delay option (must be 0)
                                                                                      0:   Delay 1 frame
                                                                                      1:   Not used

        0x3504    RSVD               –        –      Reserved

                                                         Gain Conversation Option
                                                                                      Bit[7]:    dac_finegain_highbit
                                                                                      Bit[6]:    switch_snr_gain_en
        0x3505    GCVT OPTION       0x00    RW         Bit[5:4]:  Sensor gain fixed bit
                                                                                    Bit[3:2]:  Debug (always set to 2’b11)
                                                                                   Bit[1:0]:  Sensor gain option for transferring real gain
                                                                                      to sensor gain format

        0x3506    RSVD               –        –      Reserved        Confidential
                                                                                   Bit[7:2]:  Not used      OmnivisionBit[1:0]:  Gain shift option
                                                                             00:  No shift
        0x3507    GAIN SHIFT          0x00    RW
                                                                             01:  Left shift 1 bit
                                                                             10:  Left shift 2 bit            for
                                                                                  11:   Left shift 3 bit

        0x3508    DEBUG             –        –      Debug Mode           only                                                                                    Bit[7:0]:  Gain[7:0]
                                                                                                                                 If 0x3503[2] = 0, gain[7:0] is real gain format,
                                                                  where low 4 bits are fraction bits (e.g., 0x10
                                                                                                   is 1x gain, 0x28 is 2.5x gain)

        0x3509    GAIN                0x80    RW
                                                                                                                                 If 0x3503[2] = 1, gain[7:0] is sensor gain
                                                                                format, gain[7:4] is coarse gain, 00000: 1x,
                                                                      00001: 2x, 00011: 4x, 00111: 8x, gain[7] is 1,
                                                                                    gain[3:0] is fine gain. For example, 0x10 is
                                                                    1x gain, 0x30 is 2x gain, 0x70 is 4x gain





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 82

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-5     manual AEC/AGC registers (sheet 3 of 3)

                                                                     default
                             address    register name        value   R/W    description

                             0x350A~
                                DEBUG             –        –      Debug Control
                              0x3512

                                                                                                                    Bit[7:6]:  Reserved
                              0x3519    FINE GAIN DFT       0x31    RW
                                                                                                                    Bit[5:0]:  r_fine_snr_gain_16_def_l_o

                                                                                                                    Bit[7:6]:  Reserved
                             0x351A    FINE GAIN DFT       0x20    RW
                                                                                                                    Bit[5:0]:  r_fine_snr_gain_16_def_s_o

                             0x351B    FINE GAIN LOW8     0xFF    RW         Bit[7:0]:  r_fine_snr_gain_16_low8_l_o

                             0x351C    FINE GAIN LOW8     0xFF    RW         Bit[7:0]:  r_fine_snr_gain_16_low8_s_o

                                                                                                                    Bit[7:2]:  Reserved
                             0x351D    FINE GAIN SEL       0x00    RW         Bit[1]:    r_fine_snr_gain_16_sel_s_o
                                                                                                                       Bit[0]:    r_fine_snr_gain_16_sel_l_o


                    7.6 analog control [0x3600 ~ 0x3684]


                           table 7-6     analog control registers

                                                                            default
                              address       register name          value     R/W      description

                               0x3600~     ANALOG
                                                                  –           –         Reserved
                               0x3665      REGISTERS

                                                                                FSIN/VSYNC Input and Output Select
                                                                                                                                   Bit[7:4]:  Output select
                                                                                                                 0x0:   VSYNC        Confidential
                                                                                                                  Others: For debug only
                                                                                                                                   Bit[3:0]:  Internal frame sync input      Omnivision
                                                                                                                           select
                               0x3666      ANA_CORE_6          0x0A     RW
                                                                                                                 0x0:   From FSIN pin, used
                                                                                                                                                    for both frame sync            forand frame trigger
                                                                                                                                   function
                                                                                                           0xA:    Fixed value 0
                                                                                                                  Others: For debug purposes           only
                               0x3667~     ANALOG
                                                                  –           –         Reserved
                               0x3684      REGISTERS





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 83

                                                                                                                              7-15



   7.7 sensor control [0x3700 ~ 0x37AF]


      table 7-7     sensor control registers

                                                     default
        address     register name               value    R/W    description

        0x3700~   SENSOR CONTROL
                                              –         –       Sensor Control Registers
        0x37A7    REGISTERS

        0x37A8     FIFO_CTRL0_H              0x00    RW      Internal FIFO Control

        0x37A9     FIFO_CTRL0_L              0x60    RW      Internal FIFO Control

        0x37AA~   SENSOR CONTROL
                                              –         –       Sensor Control Registers
        0x37AF    REGISTERS


   7.8 timing control [0x3800 ~ 0x3835, 0x3837]


      table 7-8     timing control registers (sheet 1 of 4)

                                                     default
        address     register name              value    R/W     description

         0x3800     TIMING_X_ADDR_START     0x00    RW      Array Horizontal Start Point High Byte

         0x3801     TIMING_X_ADDR_START     0x00    RW      Array Horizontal Start Point Low Byte

         0x3802     TIMING_Y_ADDR_START     0x00    RW      Array Vertical Start Point High Byte

         0x3803     TIMING_Y_ADDR_START     0x00    RW      Array Vertical Start Point Low Byte

         0x3804     TIMING_X_ADDR_END       0x05    RW      Array Horizontal End Point High Byte        Confidential
         0x3805     TIMING_X_ADDR_END       0x0F    RW      Array Horizontal End Point Low Byte      Omnivision
         0x3806     TIMING_Y_ADDR_END       0x03    RW      Array Vertical End Point High Byte

         0x3807     TIMING_Y_ADDR_END       0x2F    RW      Array Vertical End Point Low Byte            for
         0x3808     TIMING_X_OUTPUT_SIZE    0x05    RW     ISP Horizontal Output Width High Byte

         0x3809     TIMING_X_OUTPUT_SIZE    0x00    RW     ISP Horizontal Output Width Low Byte           only
        0x380A     TIMING_Y_OUTPUT_SIZE    0x03    RW     ISP Vertical Output Height High Byte

        0x380B     TIMING_Y_OUTPUT_SIZE    0x20    RW     ISP Vertical Output Height Low Byte

        0x380C     TIMING_HTS               0x02    RW       Total Horizontal Timing Size High Byte

        0x380D     TIMING_HTS              0xD8    RW       Total Horizontal Timing Size Low Byte

        0x380E     TIMING_VTS                0x03    RW       Total Vertical Timing Size High Byte

        0x380F     TIMING_VTS               0x8E    RW       Total Vertical Timing Size Low Byte





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 84

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-8     timing control registers (sheet 2 of 4)

                                                                              default
                             address     register name              value    R/W     description

                               0x3810     TIMING_ISP_X_WIN         0x00    RW     ISP Horizontal Windowing Offset High Byte

                               0x3811     TIMING_ISP_X_WIN         0x08    RW     ISP Horizontal Windowing Offset Low Byte

                               0x3812     TIMING_ISP_Y_WIN         0x00    RW     ISP Vertical Windowing Offset High Byte

                               0x3813     TIMING_ISP_Y_WIN         0x08    RW     ISP Vertical Windowing Offset Low Byte

                                                                                                                                  Bit[7:4]:  x_odd_inc
                               0x3814     TIMING_X_INC              0x11    RW
                                                                                                                                  Bit[3:0]:  x_even_inc

                                                                                                                                  Bit[7:4]:  y_odd_inc
                               0x3815     TIMING_Y_INC              0x11    RW
                                                                                                                                  Bit[3:0]:  y_even_inc

                              0x3816~
                                 DEBUG CTRL              –         –       Debug Control
                              0x381F

                                                                                                                                      Bit[7]:    vsub48_blc_dis
                                                                                                                                      Bit[6]:    vflip_blc
                                                                                                                                  Bit[5:3]:  Debug control
                                                                                                                                      Bit[2]:     Vflip
                               0x3820     TIMING_FORMAT1           0x40    RW
                                                                                                                                  Vertical image flip
                                                                                                                                      Bit[1]:    Vbinf
                                                                                                                                  Vertical binning
                                                                                                                                      Bit[0]:   Debug control

                                                                                                                                  Bit[7:4]:  Debug control
                                                                                                                                      Bit[3]:   Fman
                                                                                                                                      Bit[2]:    Mirr
                               0x3821     TIMING_FORMAT2           0x00    RW                   Horizontal image mirror
                                                                                                                                      Bit[1]:   Debug control
                                                                                                                                      Bit[0]:   Hbin
                                                                                                                       Horizontal binning        Confidential
                                                                                                                                  Bit[7:5]:  addr0_num[3:1]
                               0x3822     TIMING_REG22             0x46    RW
                                                                                                                                  Bit[4:0]:  ablc_num[5:1]      Omnivision

                                                                                                                                      Bit[7]:   fmt_chg_min_dly (write only)
                                                                                                                                      Bit[6]:    ext_vs_re            forBit[5]:   ext_vs_en                               0x3823     TIMING_REG23             0x00    RW
                                                                                                                                      Bit[4]:    r_init_man
                                                                                                                                      Bit[3]:    vts_no_latch
                                                                                                                                  Bit[2:0]:  ablc_adj           only
                               0x3824     TIMING_CS_RST_FSIN       0x00    RW    CS Reset Value at vs_ext High Byte

                               0x3825     TIMING_CS_RST_FSIN       0x00    RW    CS Reset Value at vs_ext Low Byte

                               0x3826     TIMING_RST_FSIN          0x00    RW    R Reset Value at vs_ext High Byte

                               0x3827     TIMING_RST_FSIN          0x00    RW    R Reset Value at vs_ext Low Byte

                               0x3828     TIMING_FVTS              0x00    RW       Fractional Vertical Timing Size High Byte

                               0x3829     TIMING_FVTS              0x00    RW       Fractional Vertical Timing Size Low Byte





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 85

                                                                                                                              7-17




      table 7-8     timing control registers (sheet 3 of 4)

                                                     default
        address     register name              value    R/W     description

                                                                                                  Bit[7:4]:  Debug control
        0x382A     TIMING_REG2A             0x00    RW           Bit[3]:    vts_auto_en
                                                                                                  Bit[1:0]:  href_w

                                                                                                  Bit[7:4]:  grp_wr_start
        0x382B     TIMING_REG2B            0xFA    RW
                                                                                                  Bit[3:0]:  tc_r_int_adj

        0x382C     TIMING_REG2C             0x05    RW           Bit[7:0]:  hts_global_tx[15:8]

        0x382D     TIMING_REG2D            0xB0    RW           Bit[7:0]:  hts_global_tx[7:0]

                                                                                                  Bit[7:3]:  Debug control
                                                                                                     Bit[2]:    r_tc_hts_blank
                                                                                                   0:   Uniform HTS
                                                                                                   1:   Separate HTS for global
                                                                                                       transfer
                                                                                                     Bit[1]:    r_blc_lines_sync_tc
        0x382E     TIMING_REG2E             0x01    RW                    0:    tc_href
                                                                                                   1:    tc_href synced to image
                                                                                                                line start
                                                                                                     Bit[0]:    r_blc_lines_sync
                                                                                                   0:    tc_href
                                                                                                   1:   blc_href synced to image
                                                                                                                line start

                                                                                                     Bit[7]:    r_pd_row_st_opt
                                                                                                   0:   auto_vbk_st
                                                                                                   1:   manu_vbk_st
                                                                                                     Bit[6]:   r_pd_row_ed_opt
                                                                                                   0:   auto_vbk_ed
                                                                                                   1:   manu_vbk_ed
                                                                                                     Bit[5]:   r_pd_ana_to_sys        Confidential0:   ana_vbk cover sys_vbk
                                                                                                   1:   ana_vbk same as
        0x382F     TIMING_REG2F             0x04    RW      Omnivision  sys_vbk
                                                                                                     Bit[4]:   r_pd_sys_to_ana
                                                                                                   0:   sys_vbk same as
                                                                                 ana_vbk            for                                                                                                   1:   sys_vbk covered by
                                                                                 ana_vbk 1
                                                                                                  Bit[3:0]:  r_pd_row_ofst[3:0]
                                                                                Auto offset after vbk_st and           only
                                                                                       before vbk_ed

         0x3830     TIMING_TC_R              –       R             Bit[7:0]:  System row counter[15:8]

         0x3831     TIMING_TC_R              –       R             Bit[7:0]  System row counter[7:0]

                                                                                                  Bit[7:0]:  pd_vbk_st[15:8]
                                                                                                    Start of power saving in vblank
         0x3832     TIMING_REG32             0x00    RW
                                                                            Upper byte reference to row
                                                                                    count





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 86

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-8     timing control registers (sheet 4 of 4)

                                                                              default
                             address     register name              value    R/W     description

                                                                                                                                  Bit[7:0]:  pd_vbk_st[7:0]
                                                                                                                                  Start of power saving in vblank
                               0x3833     TIMING_REG33             0x05    RW
                                                                                                  Lower byte reference to row
                                                                                                             count

                                                                                                                                  Bit[7:0]:  pd_vbk_ed[15:8]
                                                                                          End of power saving in vblank
                               0x3834     TIMING_REG34             0x00    RW
                                                                                                  Upper byte reference to row
                                                                                                             count

                                                                                                                                  Bit[7:0]:  pd_vbk_ed[7:0]
                                                                                          End of power saving in vblank
                               0x3835     TIMING_REG35             0x05    RW
                                                                                                  Lower byte reference to row
                                                                                                             count

                                                                                                                         Digital Binning Control
                                                                                                                                  Bit[7:5]:  Not used
                                                                                                                                      Bit[4]:   Debug control
                                                                                                                                  Bit[3:2]:  Horizontal binning control
                                                                                                           Set to 2'b00 when binning is
                                                                                                                  disabled and 2'b11 when
                               0x3837      DIGITAL BINNING CTRL      0x00    RW                  binning is enabled
                                                                                                                                      Bit[1]:    Horizontal binning summation
                                                                                                          enable
                                                                                                                                0:   Average
                                                                                                                                1:   Summation
                                                                                                                                      Bit[0]:    Horizontal digital binning
                                                                                                          enable


                    7.9 PWM and strobe control [0x3900 ~ 0x3904, 0x3910 ~ 0x391D, 0x3920 ~        Confidential
                     0x3933]      Omnivision

                           table 7-9   PWM and strobe control registers (sheet 1 of 3)            for

                                                                         default
                             address     register name          value    R/W     description           only
                                                                                                                           Bit[7:2]:  Reserved
                               0x3900     PWM_CTRL_00         0x00    RW
                                                                                                                           Bit[1:0]:  r_strobe_st_opt

                               0x3901     PWM_CTRL_01         0x01    RW           Bit[7:0]:  r_strobe_row_st[15:8]

                               0x3902     PWM_CTRL_02        0xB4    RW           Bit[7:0]:  r_strobe_row_st[7:0]

                               0x3903     PWM_CTRL_03         0x00    RW           Bit[7:0]:  r_strobe_cs_st[15:8]

                               0x3904     PWM_CTRL_04         0x00    RW           Bit[7:0]:  r_strobe_cs_st[7:0]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 87

                                                                                                                              7-19




      table 7-9   PWM and strobe control registers (sheet 2 of 3)

                                                default
        address     register name          value    R/W     description

         0x3910     PWM_CTRL_10        0xFF    RW           Bit[7:0]:  div_reg[15:8]

         0x3911     PWM_CTRL_11        0xFF    RW           Bit[7:0]:  div_reg[7:0]

                                                                                           Bit[7:0]:  duty_reg[15:8]
         0x3912     PWM_CTRL_12         0x08    RW
                                                                         0~65535: 0% ~ 100%

                                                                                           Bit[7:0]:  duty_reg[7:0]
         0x3913     PWM_CTRL_13         0x00    RW
                                                                         0~65535: 0% ~ 100%

         0x3914     PWM_CTRL_14         0x00    RW           Bit[7:0]:  led_duty_cycle_low_reg[15:8]

         0x3915     PWM_CTRL_15         0x00    RW           Bit[7:0]:  led_duty_cycle_low_reg[7:0]

                                                                                           Bit[7:2]:  Reserved
         0x3916     PWM_CTRL_16         0x00    RW
                                                                                           Bit[1:0]:  led_average_l_low_reg[9:8]

         0x3917     PWM_CTRL_17         0x00    RW           Bit[7:0]:  led_average_l_low_reg[7:0]

                                                                                           Bit[7:2]:  Reserved
         0x3918     PWM_CTRL_18         0x00    RW
                                                                                           Bit[1:0]:  average_low_reg[9:8]

         0x3919     PWM_CTRL_19         0x00    RW           Bit[7:0]:  average_low_reg[7:0]

        0x391A    PWM_CTRL_1A         0x00    RW           Bit[7:0]:  led_duty_cycle_slope_reg

        0x391B    PWM_CTRL_1B         0x74    RW           Bit[7:0]:  led_pwm_ctrl1

        0x391C    PWM_CTRL_1C        0x00    RW           Bit[7:0]:  led_pwm_ctrl0

                                                                                           Bit[7:3]:  Reserved
                                                                                              Bit[2]:    strobe_frame_vs_sel
        0x391D    PWM_CTRL_1D        0x10    RW
                                                                                              Bit[1]:   Reserved
                                                                                              Bit[0]:    pwm_polarity         0x3920     PWM_CTRL_20 Confidential0xA5    RW           Bit[7:0]:  strobe_pattern[7:0]      OmnivisionBit[7]:   strobe_frame_sign_bit
                                                                                            0:    Positive delay
         0x3921     PWM_CTRL_21         0x00    RW
                                                                                            1:   Negative delay
                                                                                            Bit[6:0]: strobe_frame_shift[30:24]            for
         0x3922     PWM_CTRL_22         0x00    RW           Bit[7:0]:  strobe_frame_shift[23:16]

         0x3923     PWM_CTRL_23         0x00    RW           Bit[7:0]:  strobe_frame_shift[15:8]           only
         0x3924     PWM_CTRL_24         0x05    RW           Bit[7:0]:  strobe_frame_shift[7:0]

         0x3925     PWM_CTRL_25         0x00    RW           Bit[7:0]:  div_value[31:24]

         0x3926     PWM_CTRL_26         0x00    RW           Bit[7:0]:  div_value[23:16]

         0x3927     PWM_CTRL_27         0x00    RW           Bit[7:0]:  div_value[15:8]

         0x3928     PWM_CTRL_28        0x1A    RW           Bit[7:0]:  div_value[7:0]

         0x3929     PWM_CTRL_29         0x01    RW           Bit[7:0]:  r_strobe_row_st[15:8]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 88

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-9   PWM and strobe control registers (sheet 3 of 3)

                                                                         default
                             address     register name          value    R/W     description

                              0x392A    PWM_CTRL_2A        0xB4    RW           Bit[7:0]:  r_strobe_row_st[7:0]

                              0x392B    PWM_CTRL_2B         0x00    RW           Bit[7:0]:  r_strobe_cs_st[15:8]

                             0x392C    PWM_CTRL_2C        0x10    RW           Bit[7:0]:  r_strobe_cs_st[7:0]

                             0x392D    PWM_CTRL_2D        0x05    RW           Bit[7:0]:  step_onerow_man[15:8]

                              0x392E    PWM_CTRL_2E        0xF2    RW           Bit[7:0]:  step_onerow_man[7:0]

                                                                                                                               Bit[7]:   r_strobe_frm_pwen
                                                                                                                               Bit[6]:    r_strobe_frm_pwst
                                                                                                                               Bit[5]:    r_strobe_pol
                              0x392F    PWM_CTRL_2F         0x40    RW
                                                                                                                               Bit[4]:    r_strobe_step_pix
                                                                                                                               Bit[3]:   r_step_onerow_precision_man
                                                                                                                           Bit[2:0]:  r_strobe_st_opt

                               0x3930     PWM_CTRL_30         –       R             Bit[7:0]:  led_duty_cycle_low[15:8]

                               0x3931     PWM_CTRL_31         –       R             Bit[7:0]:  led_duty_cycle_low[7:0]

                               0x3932     PWM_CTRL_32         –       R             Bit[7:0]:  div_value[15:8]

                               0x3933     PWM_CTRL_33         –       R             Bit[7:0]:  div_value[7:0]


                   7.10 low power mode control [0x4F00 ~ 0x4F0D, 0x4F10 ~ 0x4F14]


                           table 7-10    low power mode control registers (sheet 1 of 2)

                                                                                default        Confidential
                             address     register name                value    R/W     description      Omnivision                                                                                                                                     Bit[7:4]:  Reserved
                                                                                                                                          Bit[3]:    psv_auto_on_dis
                                                                                                                                   0:  PSV mode auto enable            forif VTS > threshold
                                                                                                                                   1:   Disable PSV auto on
                                                                                                      mode, depending on
                                                                                                       r_psv_mode_en           onlyBit[2]:   r_psv_mode_en
                              0x4F00     PSV_CTRL                   0x00    RW
                                                                                                                                         Bit[1]:   Reserved
                                                                                                                                          Bit[0]:   psv_mode
                                                                                                                                   0:   Keep sclk on, frame
                                                                                                                                timing based on sclk
                                                                                                                                   1:   Shut off sclk at
                                                                                                                                blanking, frame timing
                                                                                                                      switches to pad_clk
                                                                                                        domain





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 89

                                                                                                                              7-21




      table 7-10    low power mode control registers (sheet 2 of 2)

                                                       default
        address     register name                value    R/W     description

                                                                                                     Bit[7:4]:  Reserved
                                                                                                        Bit[3]:    tc_sof_sync_en
        0x4F01     AUTO_SLEEP_CTRL          0x00    RW
                                                                                                        Bit[2]:    vblkp_sync_dis
                                                                                                     Bit[1:0]:  blank_retime_opt

        0x4F02     HTS_PAD_CLK               0x00    RW           Bit[7:0]:  hts_pad_clk[15:8]

        0x4F03     HTS_PAD_CLK               0xB1    RW           Bit[7:0]:  hts_pad_clk[7:0]

        0x4F04     CS_CNT_INTIAL              0x00    RW           Bit[7:0]:  cs_cnt_intial[15:8]

        0x4F05     CS_CNT_INTIAL              0x0F    RW           Bit[7:0]:  cs_cnt_intial[7:0]

        0x4F06     STREAM_ST_OFFS           0x08    RW           Bit[7:0]:  r_stream_st_offs[7:0]

        0x4F07     PCHG_ST_OFFS              0x08    RW           Bit[7:0]:  r_pchg_st_offs[7:0]

        0x4F08     CLK_WINP_OFF              0x01    RW           Bit[3:0]:  r_clk_winp_off[3:0]

        0x4F09     STRM_REA_OFF              0x02    RW           Bit[3:0]:  r_strm_rear_offs[3:0]

        0x4F0A    PCHG_REA_OFFS            0x02    RW           Bit[3:0]:  r_pchg_rear_offs[3:0]

        0x4F0B    RSVD                       –         –        Reserved

        0x4F0C    PSV_AUTO_ON_THRESH      0x10    RW           Bit[7:0]:  psv_auto_on_thresh[15:8]

        0x4F0D    PSV_AUTO_ON_THRESH      0x00    RW           Bit[7:0]:  psv_auto_on_thresh[7:0]

        0x4F10     ANA_PSV_PCH               0x00    RW           Bit[7:0]:  ana_psv_pch[15:8]

         0x4F11     ANA_PSV_PCH               0x18    RW           Bit[7:0]:  ana_psv_pch[7:0]

        0x4F12     ANA_PSV_STRM             0x0F    RW           Bit[7:0]:  ana_psv_strm[15:8]

        0x4F13     ANA_PSV_STRM              0x04    RW           Bit[7:0]:  ana_psv_strm[7:0]        Confidential
                                                                                                     Bit[7:1]:  Reserved        0x4F14     ANA_PSV_INVOmnivision0x00    RW            Bit[0]:   ana_psv_inv            for   7.11 BIST [0x3E00 ~ 0x3E12]           only
      table 7-11    BIST control registers (sheet 1 of 2)

                                                       default
        address     register name                value    R/W     description

        0x3E00     START_ADDRESS             0x00    RW           Bit[7:0]:  start_address[15:8]

        0x3E01     START_ADDRESS             0x00    RW           Bit[7:0]:  start_address[7:0]

        0x3E02    END_ADDRESS              0x0F    RW           Bit[7:0]:  end_address[15:8]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 90

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-11    BIST control registers (sheet 2 of 2)

                                                                                default
                             address     register name                value    R/W     description

                              0x3E03    END_ADDRESS             0xDB    RW           Bit[7:0]:  end_address[7:0]

                              0x3E04     BIST_OP_CNT                0x14    RW           Bit[4:0]:  bist_op_cnt

                              0x3E05     IP_BIST_SPECIAL_DATA       0x00    RW           Bit[7:0]:  ip_bist_special_data

                              0x3E06     SRAM_SELECT               0x03    RW           Bit[5:0]:  sram_select

                              0x3E07     BIST_CONTROL              0x20    RW           Bit[7:0]:  bist_control

                              0x3E08     IP_BIST_ERR_ADDR          –       R              Bit[7:0]:  ip_bist_err_addr[15:8]

                              0x3E09     IP_BIST_ERR_ADDR          –       R              Bit[7:0]:  ip_bist_err_addr[7:0]

                             0x3E0A     IP_BIST_ERR_DATA           –       R              Bit[7:0]:  ip_bist_err_data

                             0x3E0B     IP_BIST_REF_DATA           –       R              Bit[7:0]:  ip_bist_ref_data

                            0x3E0C     BIST_INFO                  –       R              Bit[4:0]:  bist_info

                            0x3E0D     BIST_RESULT                –       R              Bit[7:0]:  bist_result[23:16]

                             0x3E0E     BIST_RESULT                –       R              Bit[7:0]:  bist_result[15:8]

                             0x3E0F     BIST_RESULT                –       R              Bit[7:0]:  bist_result[7:0]

                              0x3E10     BIST_TEST_MODE            –       R              Bit[7:0]:  bist_test_mode

                              0x3E11     BIST_DONE                 –       R              Bit[0]:    bist_done

                                                                                                                                     Bit[7:0]: SRAM data in read only
                              0x3E12    SRAM DATA DONE            0x00    RW
                                                                                       mode        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 91

                                                                                                                              7-23



   7.12 OTP control [0x3D80 ~ 0x3D87]


      table 7-12   OTP control registers

                                             default
        address     register name       value    R/W     description

                                                                                           Bit[7]:   otp_pgenb_o
                                                                                          0:   Not used
                                                                                          1:   Program on going
                OTP PROGRAM
        0x3D80                         0x00    RW           Bit[6:1]:  Debug control
                 CTRL
                                                                                           Bit[0]:   otp_pgm
                                                                       To start program
                                                                                 Write bit 0 to 1

                                                                                           Bit[7]:    pt_load_o
                                                                                          0:   Not used
                                                                                          1:   Load on going
        0x3D81     OTP LOAD CTRL     –       R             Bit[6:1]:  Debug control
                                                                                           Bit[0]:    otp_rd
                                                                                    Writing to this register will start loading
                                                                              data

                OTP PROGRAM                                      Bit[7:0]:  Control program strobe pulse, by
        0x3D82                         0x40    RW
                 PULSE                                         8 × Tsclk

                OTP LOAD                                             Bit[7:4]:  Not used
        0x3D83                         0x03    RW
                 PULSE                                                 Bit[3:0]:  Control load strobe pulse, by Tsclk

                                                                                           Bit[7]:   program_dis
                                                                                          0:   Enable
                                                                                          1:   Disable
        0x3D84     OPT MODE CTRL    0x01    RW           Bit[6]:   mode_select
                                                                                          0:   Auto mode
                                                                                          1:   Manual mode        ConfidentialBit[5:0]:  Memory select                OTP STARTOmnivisionBit[7:4]:  Not used
        0x3D85                         0x00    RW
                ADDR                                                   Bit[3:0]:  Start address for manual mode

                                                                                        Bit[7:4]:  Not used
        0x3D86     OTP END ADDR     0x1F    RW                                                                                        Bit[3:0]:  End address for manualformode

                                                                                        Bit[7:4]:  Not used
        0x3D87     OTP PS2CS         0x03    RW
                                                                                        Bit[3:0]: PS to CSB time control by sclk           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 92

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                   7.13 BLC control [0x4000 ~ 0x4017, 0x4020 ~ 0x403F, 0x4042 ~ 0x4049]


                           table 7-13   BLC control registers (sheet 1 of 4)

                                                                               default
                             address     register name                value   R/W    description

                                                                                                                                 Bit[7:4]:  r_off_avg_weight_o
                                                                                                                                     Bit[3]:    r_target_adj_dis_o
                              0x4000     BLC_CTRL_00              0xCF   RW          Bit[2]:   r_off_cmp_en_o
                                                                                                                                      Bit[1]:    r_dither_en_o
                                                                                                                                      Bit[0]:   r_mf_en_o

                                                                                                                                 Bit[7:6]:  r_hdr_option_o
                                                                                                                                     Bit[5]:   r_kcoef_man_en_o
                                                                                                                                     Bit[4]:   r_off_man_en_o
                              0x4001     BLC_CTRL_01                0x20    RW
                                                                                                                                     Bit[3]:    r_zero_ln_out_en_o
                                                                                                                                      Bit[2]:    r_blk_ln_out_en_o
                                                                                                                                 Bit[1:0]:  r_byp_mode_o

                                                                                                                                 Bit[7:3]:  Reserved
                              0x4002     BLC_CTRL_02                0x00    RW
                                                                                                                                  Bit[2:0]:  r_blk_lvl_target_o[10:8]

                              0x4003     BLC_CTRL_03                0x10    RW          Bit[7:0]:  r_blk_lvl_target_o[7:0]

                                                                                                                                 Bit[7:4]:  Not used
                              0x4004     BLC_CTRL_04                0x00    RW
                                                                                                                                 Bit[3:0]:  r_hwin_off_o[11:8]

                              0x4005     BLC_CTRL_05                0x02    RW          Bit[7:0]:  r_hwin_off_o[7:0]

                                                                                                                                 Bit[7:4]:  Not used
                              0x4006     BLC_CTRL_06                0x00    RW
                                                                                                                                 Bit[3:0]:  r_hwin_pad_o[11:8]

                              0x4007     BLC_CTRL_07                0x02    RW          Bit[7:0]:  r_hwin_pad_o[7:0]

                                                                                                                                 Bit[7:4]:  Reserved
                              0x4008     BLC_CTRL_08                0x00    RW
                                                                                                                                 Bit[3:0]:  r_up_bl_start_o[3:0]        Confidential
                                                                                                                                 Bit[7:4]:  Reserved      Omnivision0x4009     BLC_CTRL_09                0x07    RW                                                                                                                                 Bit[3:0]:  r_up_bl_end_o[3:0]

                                                                                                                                 Bit[7:3]:  Reserved
                             0x400A     BLC_CTRL_0A               0xFF   RW
                                                                                                                                 Bit[2:0]:  r_off_lim_th_o[10:8]            for
                             0x400B     BLC_CTRL_0B               0xFF   RW          Bit[7:0]:  r_off_lim_th_o[7:0]

                                                                                                                                 Bit[7:4]:  Reserved                             0x400C    BLC_CTRL_0C  only0x00    RW          Bit[3:0]:  r_dn_bl_start_o[3:0]

                                                                                                                                 Bit[7:4]:  Reserved
                             0x400D    BLC_CTRL_0D                0x07    RW
                                                                                                                                 Bit[3:0]:  r_dn_bl_end_o[3:0]

                                                                                                                                 Bit[7:3]:  Reserved
                             0x400E     BLC_CTRL_0E                0x00    RW
                                                                                                                                 Bit[2:0]:  r_kcoef_man_o[10:8]

                              0x400F     BLC_CTRL_0F                0x80    RW          Bit[7:0]:  r_kcoef_man_o[7:0]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 93

                                                                                                                              7-25




      table 7-13   BLC control registers (sheet 2 of 4)

                                                      default
        address     register name                value   R/W    description

                                                                                                    Bit[7]:    r_up_off_trig_en_o
                                                                                                    Bit[6]:    r_gain_chg_trig_en_o
                                                                                                    Bit[5]:    r_fmt_chg_trig_en_o
                                                                                                    Bit[4]:    r_rst_trig_en_o
        0x4010     BLC_CTRL_10                0x41    RW
                                                                                                    Bit[3]:   r_man_avg_en_o
                                                                                                    Bit[2]:    r_man_trig_o
                                                                                                    Bit[1]:    r_off_frz_en_o
                                                                                                    Bit[0]:    r_off_always_up_o

                                                                                                    Bit[7]:   Reserved
                                                                                                    Bit[6]:   r_off_chg_mf_en_o
                                                                                                    Bit[5]:   r_fmt_chg_mf_en_o
                                                                                                    Bit[4]:   r_gain_chg_mf_en_o
        0x4011     BLC_CTRL_11                0x7F    RW
                                                                                                    Bit[3]:   r_rst_mf_mode_o
                                                                                                    Bit[2]:   r_off_chg_mf_mode_o
                                                                                                    Bit[1]:   r_fmt_chg_mf_mode_o
                                                                                                    Bit[0]:   r_gain_chg_mf_mode_o

                                                                                                 Bit[7:6]:  Reserved
        0x4012     BLC_CTRL_12                0x00    RW
                                                                                                 Bit[5:0]:  r_rst_trig_fn_o

                                                                                                 Bit[7:6]:  Reserved
        0x4013     BLC_CTRL_13                0x02    RW
                                                                                                 Bit[5:0]:  r_fmt_trig_fn_o

                                                                                                 Bit[7:6]:  Reserved
        0x4014     BLC_CTRL_14                0x02    RW
                                                                                                 Bit[5:0]:  r_gain_trig_fn_o

                                                                                                 Bit[7:6]:  Reserved
        0x4015     BLC_CTRL_15                0x02    RW
                                                                                                 Bit[5:0]:  r_off_trig_fn_o

                                                                                                 Bit[7:3]:  Reserved
        0x4016     BLC_CTRL_16                0x00    RW
                                                                                                 Bit[2:0]:  r_off_trig_th_o[10:8]        0x4017     BLC_CTRL_17 Confidential0x00    RW          Bit[7:0]:  r_off_trig_th_o[7:0]        0x4020     BLC_CTRL_20Omnivision0x00    RW          Bit[7:0]:  r_off_cmp_th000_o[7:0]

        0x4021     BLC_CTRL_21                0x00    RW          Bit[7:0]:  r_off_cmp_k000_o[7:0]        0x4022     BLC_CTRL_22                0x00    RW          Bit[7:0]:  r_off_cmp_th001_o[7:0]for

        0x4023     BLC_CTRL_23                0x00    RW          Bit[7:0]:  r_off_cmp_k001_o[7:0]

        0x4024     BLC_CTRL_24                0x00    RW          Bit[7:0]:  r_off_cmp_th010_o[7:0]           only
        0x4025     BLC_CTRL_25                0x00    RW          Bit[7:0]:  r_off_cmp_k010_o[7:0]

        0x4026     BLC_CTRL_26                0x00    RW          Bit[7:0]:  r_off_cmp_th011_o[7:0]

        0x4027     BLC_CTRL_27                0x00    RW          Bit[7:0]:  r_off_cmp_k011_o[7:0]

        0x4028     BLC_CTRL_28                0x00    RW          Bit[7:0]:  r_off_cmp_th100_o[7:0]

        0x4029     BLC_CTRL_29                0x00    RW          Bit[7:0]:  r_off_cmp_k100_o[7:0]

        0x402A     BLC_CTRL_2A                0x00    RW          Bit[7:0]:  r_off_cmp_th101_o[7:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 94

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-13   BLC control registers (sheet 3 of 4)

                                                                               default
                             address     register name                value   R/W    description

                             0x402B     BLC_CTRL_2B                0x00    RW          Bit[7:0]:  r_off_cmp_k101_o[7:0]

                             0x402C    BLC_CTRL_2C                0x00    RW          Bit[7:0]:  r_off_cmp_th110_o[7:0]

                             0x402D    BLC_CTRL_2D                0x00    RW          Bit[7:0]:  r_off_cmp_k110_o[7:0]

                             0x402E     BLC_CTRL_2E                0x00    RW          Bit[7:0]:  r_off_cmp_th111_o[7:0]

                              0x402F     BLC_CTRL_2F                0x00    RW          Bit[7:0]:  r_off_cmp_k111_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved
                              0x4030     BLC_CTRL_30                0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man000_o[10:8]

                              0x4031     BLC_CTRL_31                0x00     R/W         Bit[7:0]:  r_off_man000_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved
                              0x4032     BLC_CTRL_32                0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man001_o[10:8]

                              0x4033     BLC_CTRL_33                0x00    RW          Bit[7:0]:  r_off_man001_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved
                              0x4034     BLC_CTRL_34                0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man010_o[10:8]

                              0x4035     BLC_CTRL_35                0x00    RW          Bit[7:0]:  r_off_man010_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved
                              0x4036     BLC_CTRL_36                0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man011_o[10:8]

                              0x4037     BLC_CTRL_37                0x00    RW          Bit[7:0]:  r_off_man011_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved
                              0x4038     BLC_CTRL_38                0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man100_o[10:8]

                              0x4039     BLC_CTRL_39                0x00    RW          Bit[7:0]:  r_off_man100_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved                             0x403A ConfidentialBLC_CTRL_3A                0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man101_o[10:8]      Omnivision
                             0x403B     BLC_CTRL_3B                0x00    RW          Bit[7:0]:  r_off_man101_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved
                             0x403C    BLC_CTRL_3C                0x00    RW            forBit[2:0]:  r_off_man110_o[10:8]

                             0x403D    BLC_CTRL_3D                0x00    RW          Bit[7:0]:  r_off_man110_o[7:0]

                                                                                                                                 Bit[7:3]:  Reserved                             0x403E     BLC_CTRL_3E  only0x00    RW
                                                                                                                                 Bit[2:0]:  r_off_man111_o[10:8]

                              0x403F     BLC_CTRL_3F                0x00    RW          Bit[7:0]:  r_off_man111_o[7:0]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 95

                                                                                                                              7-27




      table 7-13   BLC control registers (sheet 4 of 4)

                                                      default
        address     register name                value   R/W    description

                                                                                                    Bit[7]:    r_format_trig_beh_o
                                                                                                    Bit[6]:    r_gain_trig_beh_o
                                                                                                    Bit[5]:   r_slope_man_en_o
        0x4042     BLC_CTRL_42                0x11    RW          Bit[4]:   r_slope_en_o
                                                                                                 Bit[3:2]:  r_manu_cvdn_out_en_o
                                                                                                    Bit[1]:    r_lim_off_en_o
                                                                                                    Bit[0]:   r_mf_dn_en_o

                                                                                                    Bit[7]:    r_2bits_precision_en_o
                                                                                                    Bit[6]:    r_bot_blk_ln_en_o
                                                                                                    Bit[5]:    r_dn_off_trig_en_o
                                                                                                    Bit[4]:    r_output_sel_o
        0x4043     BLC_CTRL_43                0x40    RW
                                                                                                    Bit[3]:   r_cvdn_blc_en_man_o
                                                                                                    Bit[2]:   r_cvdn_blc_en_o
                                                                                                    Bit[1]:   r_dc_blc_en_man_o
                                                                                                    Bit[0]:   r_dc_blc_en_o

                                                                                                 Bit[7:2]:  Reserved
        0x4044     BLC_CTRL_44                0x00    RW
                                                                                                 Bit[1:0]:  r_rnd_gain_th_o[9:8]

        0x4045     BLC_CTRL_45                0x20    RW          Bit[7:0]:  r_rnd_gain_th_o[7:0]

        0x4046     BLC_CTRL_46                0x00    RW          Bit[7:0]:  r_thre_o[7:0]

                                                                                                    Bit[7]:    r_thre_en_o
        0x4047     BLC_CTRL_47                0x00    RW          Bit[6:2]:  Reserved
                                                                                                 Bit[1:0]:  r_thre_o[9:8]

                                                                                                 Bit[7:2]:  Reserved
        0x4048     BLC_CTRL_48                0x00    RW
                                                                                                 Bit[1:0]:  r_thre_man_o[9:8]

        0x4049     BLC_CTRL_49                0x00    RW          Bit[7:0]:  r_thre_man_o[7:0]        Confidential   7.14 frame control [0x4240Omnivision~ 0x4244]            for
      table 7-14    frame control registers (sheet 1 of 2)

                                                   default        address     register name            value    R/W     descriptiononly

                                                                                               Bit[7:4]:  Not used
                                                                                                  Bit[3]:    sof_sel
         0x4240     FC CTRL0                0x00    RW           Bit[2]:    fcnt_eof_sel
                                                                                                  Bit[1]:    fcnt_mask_dis
                                                                                                  Bit[0]:    fcnt_reset

                                                                                               Bit[7:4]:  Not used
         0x4241     FRAME ON NUM          0x00    RW
                                                                                               Bit[3:0]:  Frame on number





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 96

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-14    frame control registers (sheet 2 of 2)

                                                                            default
                             address     register name            value    R/W     description

                                                                                                                               Bit[7:4]:  Not used
                               0x4242     FRAME OFF NUM         0x00    RW
                                                                                                                               Bit[3:0]:  Frame off number

                                                                                                                                   Bit[7]:   Not used
                                                                                                                                   Bit[6]:    rblue_mask_dis
                                                                                                                                   Bit[5]:   data_mask_dis
                                                                                                                                   Bit[4]:    valid_mask_dis
                               0x4243     FC CTRL3                0x00    RW
                                                                                                                                   Bit[3]:    href_mask_dis
                                                                                                                                   Bit[2]:   eof_mask_dis
                                                                                                                                   Bit[1]:   sof_mask_dis
                                                                                                                                   Bit[0]:    all_mask_dis

                               0x4244     FRAME COUNTER        –       R             Bit[7:0]:  Frame count


                   7.15 format control [0x4300 ~ 0x4307, 0x4311 ~ 0x4317, 0x4320, 0x4322 ~
                      0x4329]


                           table 7-15    format control registers (sheet 1 of 2)

                                                                        default
                             address     register name         value    R/W     description

                               0x4300     DATA_MAX H         0xFF    RW           Bit[7:0]:  Data max[9:2]

                               0x4301     DATA_MIN H          0x00    RW           Bit[7:0]:  Data min[9:2]

                                                                                                                          Bit[7:4]:  Not used
                               0x4302     CLIP L              0x0C    RW           Bit[3:2]:  Data max[1:0]        Confidential
                                                                                                                          Bit[1:0]:  Data min[1:0]      Omnivision                                                                                                                              Bit[7]:    r_inc_en
                                                                                                                              Bit[6]:    r_inc_pattern
                                                                                                                              Bit[5]:    r_pad_lsb                                                                                                                              Bit[4]:forr_bar_mux                               0x4303    FORMAT CTRL3       0x00    RW
                                                                                                                              Bit[3]:    r_bar_en
                                                                                                                              Bit[2]:    r_bit_shift_tst_en
                                                                                                                              Bit[1]:    r_tst_bit8           onlyBit[0]:    r_bit_shift_tst_md

                                                                                                                              Bit[7]:   Not used
                                                                                                                          Bit[6:4]:  data_bit_swap
                               0x4304    FORMAT CTRL4       0x08    RW
                                                                                                                              Bit[3]:    tst_full_win
                                                                                                                          Bit[2:0]:  bar_pad

                              0x4305~
                                 RSVD                –         –       Reserved
                               0x4306





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 97

                                                                                                                              7-29




      table 7-15    format control registers (sheet 2 of 2)

                                               default
        address     register name         value    R/W     description

                                                                                          Bit[7:4]:  embed_st
                                                                                            Bit[3]:   emb_st_man
         0x4307    EMBED_CTRL         0x30    RW           Bit[2]:   Reserved
                                                                                            Bit[1]:   emb_byte_order
                                                                                            Bit[0]:   emb_en

                                                                                          Bit[7:0]: VSYNC width[15:8]
         0x4311     VSYNC_WIDTH_H     0x04    RW
                                                                                                       (in terms of pixel numbers)

                                                                                          Bit[7:0]: VSYNC width[7:0]
         0x4312     VSYNC_WIDTH_L      0x00    RW
                                                                                                       (in terms of pixel numbers)

                                                                                          Bit[7:5]:  Not used
                                                                                            Bit[4]:   VSYNC polarity
         0x4313     VSYNC_CTRL         0x00    RW           Bit[3:2]: VSYNC output select
                                                                                            Bit[1]:   VSYNC mode 3
                                                                                            Bit[0]:   VSYNC mode 2

         0x4314     VSYNC_DELAY1       0x00    RW           Bit[7:0]: VSYNC trigger to VSYNC delay[23:16]

         0x4315     VSYNC_DELAY2       0x01    RW           Bit[7:0]: VSYNC trigger to VSYNC delay[15:8]

         0x4316     VSYNC_DELAY3       0x00    RW           Bit[7:0]: VSYNC trigger to VSYNC delay[7:0]

                    MIPI/DVP MODE                                        Bit[7:1]:  Reserved
         0x4317                           0x00    RW
                 OPTION                                                    Bit[0]:   DVP enable

                                                                                          Bit[7:6]:  Pixel order
                                                                                  00:  P3P4/P1P2
                                                                                  01:  P4P3/P2P1
                                                                                  10:  P1P2/P3P4
                                                                                      11:  P2P1/P4P3
                 TST PATTERN
         0x4320                           0x80    RW           Bit[5]:   byte_swap
                 CTRL        ConfidentialBit[4:2]:  Debug control
                                                                                             Bit[1]:    Solid test pattern enable      Omnivision0:    Solid test pattern OFF
                                                                                           1:    Solid test pattern enable
                                                                                             Bit[0]:   Debug control         0x4322     SOLID_P1_H          0x00    RW     P1 Value for Solid Test PatternforMSB

         0x4323     SOLID_P1_L          0x00    RW     P1 Value for Solid Test Pattern LSB         0x4324     SOLID_P2_H          0x00    RW     P2 Value foronlySolid Test Pattern MSB

         0x4325     SOLID_P2_L          0x00    RW     P2 Value for Solid Test Pattern LSB

         0x4326     SOLID_P4_H          0x00    RW     P4 Value for Solid Test Pattern MSB

         0x4327     SOLID_P4_L          0x00    RW     P4 Value for Solid Test Pattern LSB

         0x4328     SOLID_P3_H          0x00    RW     P3 Value for Solid Test Pattern MSB

         0x4329     SOLID_P3_L          0x00    RW     P3 Value for Solid Test Pattern LSB





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 98

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                   7.16 VFIFO control [0x4600 ~ 0x4602]


                           table 7-16   VFIFO control registers

                                                                               default
                              address       register name             value     R/W      description

                               0x4600      READ START H            0x00     RW      VFIFO Read Start Point High Byte

                               0x4601      READ START L            0x04     RW      VFIFO Read Start Point Low Byte

                                                                                                                                        Bit[7:4]:  r_rm
                                                                                                                                            Bit[3]:    r_test1
                               0x4602       VFIFO CTRL2             0xF2     RW             Bit[2]:   Debug control
                                                                                                                                            Bit[1]:   Frame reset enable
                                                                                                                                            Bit[0]:  RAM bypass enable


                   7.17 DVP control [0x4701 ~ 0x4709, 0x470C, 0x470F]


                           table 7-17   DVP control registers (sheet 1 of 2)

                                                                               default
                              address       register name             value     R/W      description

                                                                                                                                        Bit[7:2]:  Reserved
                                                                                                                                       Bit[1:0]: VSYNC output select
                                                                                                                       00:  eof_o
                               0x4701      VSYNCOUT_SEL           0x00     RW
                                                                                                                       01:  Output gpo_vsync
                                                                                                                       10:  Output hsync_o
                                                                                                                              11:  eof_o        Confidential
                                                                                                                                        Bit[7:0]:  vsync_rise_lnt[15:8]
                               0x4702      VSYNC_RISE_LNT         0x00     RW                   Line counter that controls      Omnivision
                                                                                                                                            rising position of VSYNC

                                                                                                                                        Bit[7:0]:  vsync_rise_lnt[7:0]                               0x4703      VSYNC_RISE_LNT         0x02     RWforLine counter that controls
                                                                                                                                            rising position of VSYNC

                                                                                                                                        Bit[7:0]:  vsync_fall_lnt[15:8]                               0x4704      VSYNC_FALL_LNT only0x00     RW                   Line counter that controls
                                                                                                                                                     falling position of VSYNC

                                                                                                                                        Bit[7:0]:  vsync_fall_lnt[7:0]
                               0x4705      VSYNC_FALL_LNT         0x06     RW                   Line counter that controls
                                                                                                                                                     falling position of VSYNC

                                                                                                                                        Bit[7:0]:  vsync_chg_pcnt[15:8]
                               0x4706      VSYNC_CHG_PCNT        0x00     RW             VSYNC change position
                                                                                                                             indicated by pixel position





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 99

                                                                                                                              7-31




      table 7-17   DVP control registers (sheet 2 of 2)

                                                      default
         address       register name             value     R/W      description

                                                                                                       Bit[7:0]:  vsync_chg_pcnt[7:0]
         0x4707      VSYNC_CHG_PCNT        0x10     RW             VSYNC change position
                                                                                                 indicated by pixel position

                                                                                                           Bit[7]:    Clock DDR mode enable
                                                                                                           Bit[6]:   hts_man_en
                                                                                                           Bit[5]:   VSYNC gate enable
                                                                                                           Bit[4]:   HREF gate enable
         0x4708      POLARITY_CTRL           0x09     RW
                                                                                                           Bit[3]:    r_fo_nofrst
                                                                                                          Bit[2]:   HREF polarity
                                                                                                          Bit[1]:   VSYNC polarity
                                                                                                          Bit[0]:   PCLK polarity

                                                                                                          Bit[7]:   Reserved
         0x4709      BIT_TEST_ORDER         0x00     RW             Bit[6:4]:  Data bit swap
                                                                                                       Bit[3:0]:  Reserved

                                                                                                       Bit[7:2]:  Reserved
         0x470C      R_READ_CTRL            0x81     RW             Bit[1]:    first_lv_sel
                                                                                                           Bit[0]:    r_lpcnt_free

                                                                                                       Bit[7:5]:  Reserved
         0x470F      BYP_SEL                 0x00     RW             Bit[4]:    href_sel
                                                                                                       Bit[3:0]:  bypass_sel        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 100

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                   7.18 MIPI top [0x4800 ~ 0x4808, 0x4810 ~ 0x483D, 0x484A~ 0x484F]


                           table 7-18    MIPI top control registers (sheet 1 of 8)

                                                                           default
                             address    register name             value   R/W    description

                                                                                                                             Bit[7:6]:  Reserved
                                                                                                                                Bit[5]:   gate_sc_en
                                                                                                                           0:   Clock lane is free running
                                                                                                                           1:   Gate clock lane when there is
                                                                                                 no packet to transmit
                                                                                                                                Bit[4]:    line_sync_en
                                                                                                                           0:  Do not send line short packet
                                                                                                                                          for each line
                                                                                                                           1:   Send line short packet for each
                                                                                                                                          line
                                                                                                                                Bit[2]:    pclk_inv_o
                              0x4800    MIPI_CTRL00              0x04    RW                    0:   Use falling edge of mipi_pclk_o
                                                                                                                                 to generate MIPI bus to PHY
                                                                                                                           1:   Use rising edge of mipi_pclk_o
                                                                                                                                 to generate MIPI bus to PHY
                                                                                                                                 Bit[1]:     firt_bit
                                                                                        Change clk_lane first bit
                                                                                                                           0:   Output 8'h55
                                                                                                                           1:   Output 8'hAA
                                                                                                                                Bit[0]:   LPX_select for pclk domain
                                                                                                                           0:   Auto calculate t_lpx_p, unit
                                                                                                                  pclk2x cycle
                                                                                                                           1:   Use lpx_p_min[7:0]

                              0x4801    RSVD                    –        –       Reserved        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 101

                                                                                                                              7-33




      table 7-18    MIPI top control registers (sheet 2 of 8)

                                                  default
        address    register name             value   R/W    description

                                                                                               Bit[7]:    hs_prepare_sel
                                                                                              0:   Auto calculate T_hs_prepare,
                                                                                                       unit pclk2x
                                                                                              1:   Use hs_prepare_min_o[7:0]
                                                                                               Bit[6]:    clk_prepare_sel
                                                                                              0:   Auto calculate T_clk_prepare,
                                                                                                       unit pclk2x
                                                                                              1:   Use clk_prepare_min_o[7:0]
                                                                                               Bit[5]:    clk_post_sel
                                                                                              0:   Auto calculate T_clk_post, unit
                                                                                        pclk2x
                                                                                              1:   Use clk_post_min_o[7:0]
                                                                                               Bit[4]:    clk_trail_sel
                                                                                              0:   Auto calculate T_clk_trail, unit
                                                                                        pclk2x
                                                                                              1:   Use clk_trail_min_o[7:0]
        0x4802    MIPI_CTRL02              0x00    RW
                                                                                               Bit[3]:    hs_exit_sel
                                                                                              0:   Auto calculate T_hs_exit, unit
                                                                                        pclk2x
                                                                                              1:   Use hs_exit_min_o[7:0]
                                                                                               Bit[2]:    hs_zero_sel
                                                                                              0:   Auto calculate T_hs_zero, unit
                                                                                        pclk2x
                                                                                              1:   Use hs_zero_min_o[7:0]
                                                                                               Bit[1]:    hs_trail_sel
                                                                                              0:   Auto calculate T_hs_trail, unit
                                                                                        pclk2x
                                                                                              1:   Use hs_trail_min_o[7:0]
                                                                                               Bit[0]:    clk_zero_sel
                                                                                              0:   Auto calculate T_clk_zero, unit
                                                                                        pclk2x        Confidential1:   Use clk_zero_min_o[7:0]      OmnivisionBit[7:5]:  Reserved
                                                                                               Bit[4]:   pu_mark_en_o
                                                                                               Bit[3]:   manu_ofset_o
        0x4803    MIPI_CTRL03              0x10    RW                   t_perio manual offset SMIA            for
                                                                                               Bit[2]:   r_manual_half2one
                                                                                       t_period half to 1 SMIA
                                                                                            Bit[1:0]:  Reserved           only
        0x4804    RSVD                    –        –       Reserved

                                                                                            Bit[7:4]:  Reserved
                                                                                               Bit[3]:   lpda_retim_manu_o
                                                                                               Bit[2]:    lpda_retim_sel_o
        0x4805    MIPI_CTRL05              0x00    RW                    1:   Manual
                                                                                               Bit[1]:   lpck_retim_manu_o
                                                                                               Bit[0]:    lpck_retim_sel_o
                                                                                              1:   Manual





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 102

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-18    MIPI top control registers (sheet 3 of 8)

                                                                           default
                             address    register name             value   R/W    description

                                                                                                                             Bit[7:5]:  Reserved
                                                                                                                                Bit[4]:   pu_mark_en_o
                                                                                           Power up mark1 enable
                                                                                                                                 Bit[3]:    mipi_remot_rst
                              0x4806    MIPI_CTRL06              0x00    RW          Bit[2]:    mipi_susp
                                                                                                                                Bit[1]:    mipi_ul_auto_en
                                                                                                                                 Bit[0]:    tx_lsb_first
                                                                                                                           0:   High bit first
                                                                                                                           1:   Low power transmit low bit first

                                                                                                                             Bit[7:4]:  Reserved
                              0x4807    MIPI_CTRL07              0x03    RW          Bit[3:0]:  sw_t_lpx
                                                                                                                         ul_tx T_lpx

                                                                                                                             Bit[7:0]:  wkup_dly
                              0x4808    MIPI_CTRL08              0x18    RW
                                                                                            Mark1 wakeup delay/2^10

                                                                                         High Byte of Maximum Frame Counter of Frame
                              0x4810    MIPI_FCNT_MAX          0xFF    RW
                                                                                  Sync Short Packet

                                                                           Low Byte of Maximum Frame Counter of Frame
                               0x4811    MIPI_FCNT_MAX          0xFF    RW
                                                                                  Sync Short Packet

                              0x4812    RSVD                    –        –       Reserved

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                                Bit[2]:    vc_sel
                              0x4813    MIPI_CTRL13              0x00    RW                  Input VC or reg VC
                                                                                                                            Bit[1:0]: VC
                                                                                                                               Virtual channel of MIPI

                                                                                                                                 Bit[7]:   Reserved
                                                                                                                                Bit[6]:    lpkt_dt_sel        Confidential 0:   Use mipi_dt
                              0x4814    MIPI_CTRL14             0x2A    RW                    1:   Use dt_man_o as long packet      Omnivision                       data
                                                                                                                            Bit[5:0]:  dt_man
                                                                                              Manual data type            for                              0x4815~
                                RSVD                    –        –       Reserved
                              0x4817

                                                                                                                             Bit[7:2]:  Reserved           only
                                                                                                                            Bit[1:0]:  hs_zero_min[9:8]
                              0x4818    MIPI_HS_ZERO_MIN        0x00    RW
                                                                                                   High byte of minimum value of
                                                                                                           hs_zero, unit ns

                                                                                                                            Bit[7:0]:  hs_zero_min[7:0]
                                                                                    Low byte of minimum value of
                              0x4819    MIPI_HS_ZERO_MIN        0x70    RW                 hs_zero, unit ns
                                                                                                          hs_zero_real = hs_zero_min_o +
                                                                                                       Tui*ui_hs_zero_min_o





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 103

                                                                                                                              7-35




      table 7-18    MIPI top control registers (sheet 4 of 8)

                                                  default
        address    register name             value   R/W    description

                                                                                            Bit[7:2]:  Reserved
                                                                                            Bit[1:0]:  hs_trail_min[9:8]
        0x481A    MIPI_HS_TRAIL_MIN       0x00    RW
                                                                            High byte of minimum value of
                                                                                                    hs_trail, unit ns

                                                                                            Bit[7:0]:  hs_trail_min[7:0]
                                                                Low byte of minimum value of
        0x481B    MIPI_HS_TRAIL_MIN       0x3C   RW                     hs_trail, unit ns
                                                                                             hs_trail_real = hs_trail_min_o +
                                                                                      Tui*ui_hs_trail_min_o

                                                                                            Bit[7:2]:  Reserved
                                                                                            Bit[1:0]:  clk_zero_min[9:8]
        0x481C    MIPI_CLK_ZERO_MIN       0x01    RW
                                                                            High byte of minimum value of
                                                                                        clk_zero, unit ns

                                                                                            Bit[7:0]:  clk_zero_min[7:0]
                                                                Low byte of minimum value of
        0x481D    MIPI_CLK_ZERO_MIN      0x2C   RW                  clk_zero, unit ns
                                                                                     clk_zero_real = clk_zero_min_o +
                                                                                 Tui*ui_clk_zero_min_o

                                                                                            Bit[7:0]:  clk_prepare_max
                 MIPI_CLK_PREPARE_
        0x481E                             0x5F    RW             Maximum value of clk_prepare, unit
                 MIN
                                                                          ns

                                                                                            Bit[7:0]:  clk_prepare_min
                 MIPI_CLK_PREPARE_
        0x481F                              0x26    RW              Minimum value of clk_prepare, unit
                 MIN
                                                                          ns

                                                                                            Bit[7:2]:  Reserved
                                                                                            Bit[1:0]:  clk_post_min[9:8]
        0x4820    MIPI_CLK_POST_MIN       0x00    RW
                                                                            High byte of minimum value of        Confidential
                                                                                         clk_post, unit ns      OmnivisionBit[7:0]:  clk_post_min[7:0]
                                                                Low byte of minimum value of
        0x4821    MIPI_CLK_POST_MIN      0x3C   RW                   clk_post, unit ns                                                                                      clk_post_real = forclk_post_min_o +
                                                                                  Tui*ui_clk_post_min_o

                                                                                            Bit[7:2]:  Reserved                                                                                            Bit[1:0]:onlyclk_trail_min[9:8]        0x4822    MIPI_CLK_TRAIL_MIN      0x00    RW
                                                                            High byte of minimum value of
                                                                                                          clk_trail, unit ns

                                                                                            Bit[7:0]:  clk_trail_min[7:0]
                                                                Low byte of minimum value of
        0x4823    MIPI_CLK_TRAIL_MIN      0x3C   RW                      clk_trail, unit ns
                                                                                                 clk_trail_real = clk_trail_min_o +
                                                                                        Tui*ui_clk_trail_min_o





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 104

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-18    MIPI top control registers (sheet 5 of 8)

                                                                           default
                             address    register name             value   R/W    description

                                                                                                                             Bit[7:2]:  Reserved
                                                                                                                            Bit[1:0]:  lpx_p_min[9:8]
                              0x4824    MIPI_LPX_P_MIN           0x00    RW
                                                                                                   High byte of minimum value of lpx_p,
                                                                                                                               unit ns

                                                                                                                            Bit[7:0]:  lpx_p_min[7:0]
                                                                                    Low byte of minimum value of lpx_p,
                              0x4825    MIPI_LPX_P_MIN           0x32    RW                    unit ns
                                                                                                               lpx_p_real =
                                                                                                   lpx_p_min_o + Tui*ui_lpx_p_min_o

                                                                                                                             Bit[7:0]:  hs_prepare_min
                              0x4826    MIPI_HS_PREPARE_MIN    0x32    RW              Minimum value of hs_prepare, unit
                                                                                                 ns

                                                                                                                             Bit[7:0]:  hs_prepare_max
                              0x4827    MIPI_HS_PREPARE_MAX   0x55    RW             Maximum value of hs_prepare, unit
                                                                                                 ns

                                                                                                                             Bit[7:2]:  Reserved
                                                                                                                             Bit[1:0]:  hs_exit_min[9:8]
                              0x4828     MIPI_HS_EXIT_MIN         0x00    RW
                                                                                                   High byte of minimum value of
                                                                                                                       hs_exit, unit ns

                                                                                                                             Bit[7:0]:  hs_exit_min[7:0]
                                                                                    Low byte of minimum value of
                              0x4829     MIPI_HS_EXIT_MIN         0x64    RW                   hs_exit, unit ns
                                                                                                                  hs_exit_real = hs_exit_min_o +
                                                                                                            Tui*ui_hs_exit_min_o

                             0x482A    MIPI_UI_HS_ZERO_MIN     0x06    RW     Minimum UI Value of hs_zero, unit UI

                             0x482B    MIPI_UI_HS_TRAIL_MIN     0x04    RW     Minimum UI Value of hs_trail, unit UI        Confidential
                             0x482C    MIPI_UI_CLK_ZERO_MIN    0x00    RW     Minimum UI Value of clk_zero, unit UI      Omnivision
                                      MIPI_UI_CLK_PREPARE_
                             0x482D                              0x00    RW     Minimum UI Value of clk_prepare, unit UI
                                     MIN                             0x482E    MIPI_UI_CLK_POST_MIN    0x34    RW     MinimumforUI Value of clk_post, unit UI

                              0x482F    MIPI_UI_CLK_TRAIL_MIN    0x00    RW     Minimum UI Value of clk_trail, unit UI

                                                                               Minimum UI Value of lpx_p (pclk2x domain), unit                              0x4830     MIPI_UI_LPX_P_MIN        0x00onlyRW
                                                                                           UI

                                                                                                                             Bit[7:4]:  ui_hs_prepare_max
                                                                                   Maximum UI value of hs_prepare,
                                                                                                                               unit UI
                              0x4831    MIPI_UI_HS_PREPARE      0x64    RW
                                                                                                                            Bit[3:0]:  ui_hs_prepare_min
                                                                                         Minimum UI value of hs_prepare,
                                                                                                                               unit UI

                              0x4832     MIPI_UI_HS_EXIT_MIN      0x00    RW     Minimum UI Value of hs_exit, unit UI





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 105

                                                                                                                              7-37




      table 7-18    MIPI top control registers (sheet 6 of 8)

                                                  default
        address    register name             value   R/W    description

                                                                                            Bit[7:6]:  Reserved
        0x4833    MIPI_PKT_START_SIZE     0x06    RW
                                                                                            Bit[5:0]:  mipi_pkt_start_size[5;0]

        0x4834~
               RSVD                    –        –       Reserved
        0x4836

        0x4837    MIPI_PCLK_PERIOD        0x10    RW      Period of pclk2x, pclk_div=1, and 1 bit decimal

                                                                                               Bit[7]:    lp_sel0
                                                                                              0:   Auto gen mipi_lp_dir0_o
                                                                                              1:   Use lp_dir_man0 to be
                                                                                         mipi_lp_dir0_o
                                                                                               Bit[6]:    lp_dir_man0
                                                                                              0:   Input
                                                                                              1:   Output
                                                                                               Bit[5]:   lp_p0_o
                                                                                               Bit[4]:   lp_n0_o
        0x4838     MIPI_LP_GPIO0            0x00    RW
                                                                                               Bit[3]:    lp_sel1
                                                                                              0:   Auto gen mipi_lp_dir1_o
                                                                                              1:   Use lp_dir_man1 to be
                                                                                         mipi_lp_dir1_o
                                                                                               Bit[2]:    lp_dir_man1
                                                                                              0:   Input
                                                                                              1:   Output
                                                                                               Bit[1]:   lp_p1_o
                                                                                               Bit[0]:   lp_n1_o

                                                                                               Bit[7]:    lp_sel2
                                                                                              0:   Auto gen mipi_lp_dir2_o
                                                                                              1:   Use lp_dir_man2 to be
                                                                                         mipi_lp_dir2_o
                                                                                               Bit[6]:    lp_dir_man2        Confidential0:   Input
                                                                                              1:   Output      OmnivisionBit[5]:   lp_p2_o
                                                                                               Bit[4]:   lp_n2_o
        0x4839     MIPI_LP_GPIO1            0x00    RW
                                                                                               Bit[3]:    lp_sel3
                                                                                              0:   Auto gen mipi_lp_dir3_o            for
                                                                                              1:   Use lp_dir_man3 to be
                                                                                         mipi_lp_dir3_o
                                                                                               Bit[2]:    lp_dir_man3           only0:   Input
                                                                                              1:   Output
                                                                                               Bit[1]:   lp_p3_o
                                                                                               Bit[0]:   lp_n3_o

        0x483A~
               RSVD                    –        –       Reserved
        0x483B

                                                                                            Bit[7:4]:  Reserved
        0x483C    MIPI_CTRL33              0x02    RW          Bit[3:0]:  t_clk_pre
                                                                                                 unit pclk2x cycle





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 106

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-18    MIPI top control registers (sheet 7 of 8)

                                                                           default
                             address    register name             value   R/W    description

                                                                                                                                Bit[7]:    lp_ck_sel0
                                                                                                                           0:   Auto gen mipi_ck_lp_dir0_o
                                                                                                                           1:   Use lp_ck_dir_man0 to be
                                                                                                                 mipi_ck_lp_dir0_o
                                                                                                                                Bit[6]:   lp_ck_dir_man0
                                                                                                                           0:   Input
                                                                                                                           1:   Output
                                                                                                                                Bit[5]:   lp_ck_p0_o
                                                                                                                                Bit[4]:   lp_ck_n0_o
                             0x483D    MIPI_LP_GPIO4            0x00    RW
                                                                                                                                Bit[3]:    lp_ck_sel1
                                                                                                                           0:   Auto gen mipi_ck_lp_dir1_o
                                                                                                                           1:   Use lp_ck_dir_man1 to be
                                                                                                                 mipi_ck_lp_dir1_o
                                                                                                                                Bit[2]:   lp_ck_dir_man1
                                                                                                                           0:   Input
                                                                                                                           1:   Output
                                                                                                                                Bit[1]:   lp_ck_p1_o
                                                                                                                                Bit[0]:   lp_ck_n1_o

                                                                                                                             Bit[7:6]:  Reserved
                                                                                                                                Bit[5]:   slp_lp_pon_man_o
                                                                                                       Set for power up
                                                                                                                                Bit[4]:    slp_lp_pon_da
                                                                                                                                Bit[3]:    slp_lp_pon_ck
                             0x484A    MIPI_CTRL4A             0x3F    RW
                                                                                                                                Bit[2]:    mipi_slp_man_st
                                                                                                    MIPI bus status manual control
                                                                                                      enable in sleep mode
                                                                                                                                Bit[1]:    clk_lane_state
                                                                                                                                 Bit[0]:    data_lane_state

                                                                                                                             Bit[7:2]:  Reserved
                                                                                                                                Bit[1]:    clk_start_sel_o        Confidential
                                                                                                                           0:   Clock starts after SOF
                                                                                                                           1:   Clock starts after reset      Omnivision0x484B    MIPI_CTRL4B              0x07    RW
                                                                                                                                Bit[0]:    sof_sel_o
                                                                                                                           0:   Frame starts after HREF
                                                                                                             occurs            for1:   Frame starts after SOF

                                                                                                                             Bit[7:4]:  Reserved
                                                                                                                                Bit[3]:   SMIA fcnt_i select           onlyBit[2]:   PRBS enable
                                                                                                                                Bit[1]:    hs_test_only
                             0x484C    MIPI_CTRL4C              0x00    RW
                                                                                                    MIPI high speed only test mode
                                                                                                      enable
                                                                                                                                Bit[0]:    Set frame count to inactive mode
                                                                                                      (keep 0)

                             0x484D    TEST_PATTEN_DATA       0xB6    RW     Data Lane Test Pattern Register

                             0x484E    FE_DLY                   0x10    RW      Last Packet to Frame End Delay / 2





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 107

                                                                                                                              7-39




      table 7-18    MIPI top control registers (sheet 8 of 8)

                                                  default
        address    register name             value   R/W    description

        0x484F    TEST_PATTEN_CK_DATA   0x55    RW      clk_test_patten_reg


   7.19 ISP top [0x5000 ~ 0x5018, 0x5020 ~ 0x5024, 0x5030 ~ 0x5035, 0x5E00
           ~ 0x5E2E]


      table 7-19    ISP top registers (sheet 1 of 6)

                                              default
        address       register name      value     R/W      description

                                                                                            Bit[7:6]:  isp_sof_sel
                                                                                               Bit[5]:    isp_eof_sel
                                                                                               Bit[4]:   bc_en
         0x5000        ISP CTRL 00       0x9F     RW             Bit[3]:   wc_en
                                                                                               Bit[2]:   dpc_buf_en
                                                                                               Bit[1]:   awbg_en
                                                                                               Bit[0]:    blc_en

                                                                                               Bit[7]:   dgc_en
                                                                                            Bit[6:5]:  Reserved
                                                                                               Bit[4]:    latch_en
         0x5001        ISP CTRL 01        0x00     RW             Bit[3]:   r_size_man
                                                                                               Bit[2]:    r_pre_isp_raw_en
                                                                                               Bit[1]:   bypass_isp1
                                                                                               Bit[0]:   bypass_isp0

                                                                                            Bit[7:3]:  Reserved
         0x5002        ISP CTRL 02        0x00     RW        ConfidentialBit[2:0]:  manual_x_addr_st[10:8]

         0x5003        ISP CTRL 03        0x00     RW            Bit[7:0]:  manual_x_addr_st[7:0]      Omnivision

                                                                                            Bit[7:1]:  Reserved
         0x5004        ISP CTRL 04        0x00     RW
                                                                                            Bit[1:0]:  manual_y_addr_st[9:8]            for
         0x5005        ISP CTRL 05        0x00     RW            Bit[7:0]:  manual_y_addr_st[7:0]

                                                                                            Bit[7:3]:  Reserved
         0x5006        ISP CTRL 06        0x05     RW
                                                                                            Bit[2:0]:  manual_x_addr_end[10:8]           only
         0x5007        ISP CTRL 07       0x0F     RW            Bit[7:0]:  manual_x_addr_end[7:0]

                                                                                            Bit[7:1]:  Reserved
         0x5008        ISP CTRL 08        0x03      R/W
                                                                                            Bit[1:0]:  manual_y_addr_end[9:8]

         0x5009        ISP CTRL 09       0x2F      R/W           Bit[7:0]:  manual_y_addr_end[7:0]

        0x500A~
                 RSVD             –          –         Reserved
        0x500F





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 108

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-19    ISP top registers (sheet 2 of 6)

                                                                       default
                             address       register name      value     R/W      description

                               0x5010       LINEAR GAIN       –        R               Bit[1:0]: AEC linear gain debug[9:8]

                                0x5011       LINEAR GAIN       –        R               Bit[7:0]: AEC linear gain debug[7:0]

                               0x5012      AWB_GAIN_R      –        R               Bit[3:0]: AWB gain R channel[11:8]

                               0x5013      AWB_GAIN_R      –        R               Bit[7:0]: AWB gain R channel[7:0]

                               0x5014      AWB_GAIN_G      –        R               Bit[3:0]: AWB gain G channel[11:8]

                               0x5015      AWB_GAIN_G      –        R               Bit[7:0]: AWB gain G channel[7:0]

                               0x5016      AWB_GAIN_B      –        R               Bit[3:0]: AWB gain B channel[11:8]

                               0x5017      AWB_GAIN_B      –        R               Bit[7:0]: AWB gain B channel[7:0]

                               0x5018      SENSOR BIAS      –        R        Sensor Bias Debug

                                                                                                                                 Bit[7]:   Reserved
                                                                                                                                 Bit[6]:   blc_px_man_en
                                                                                                                             Bit[5:4]:  blc_px_man
                               0x5020       ISP_CTRL_20       0x00     RW
                                                                                                                                Bit[3]:    blc_vsync_sel
                                                                                                                                 Bit[2]:   dig_gain_blc_man
                                                                                                                             Bit[1:0]:  dig_gain_blc

                                                                                                                             Bit[7:4]:  Reserved
                                                                                                                                 Bit[3]:   r_blc_rblue_man_en
                                                                                                                                 Bit[2]:    r_blc_rblue_man
                               0x5021       ISP_CTRL_21       0x00     RW             Bit[1]:    r_blc_target_sel
                                                                                                                           0:    blc_target[7:0]
                                                                                                                           1:    blc_target[9:2]
                                                                                                                                 Bit[0]:   r_bias_man_en

                               0x5022       ISP_CTRL_22       0x00     RW            Bit[7:0]:  r_bias_man_value        Confidential
                                                                                                                             Bit[7:6]:  Reserved      Omnivision            Bit[5]:    r_dig_4x
                               0x5023       ISP_CTRL_23       0x02     RW             Bit[4]:    r_dig_2x
                                                                                                                             Bit[3:2]:  Reserved
                                                                                                                             Bit[1:0]:  r_dig_comp[9:8]            for
                               0x5024       ISP_CTRL_24       0x00     RW            Bit[7:0]:  r_dig_comp[7:0]

                                                                                                                                 Bit[7]:   Reserved           onlyBit[6]:   dpc_px_man_en
                                                                                                                             Bit[5:4]:  dpc_px_man
                               0x5030       ISP_CTRL_30       0x00     RW             Bit[3]:    r_cen
                                                                                                                                 Bit[2]:    r_cen_sel
                                                                                                                                 Bit[1]:   dpc_size_man
                                                                                                                                 Bit[0]:    dpc_data_switch

                               0x5031       ISP_CTRL_31       0x00     RW            Bit[7:0]:  dpc_hsize_in[7:0]

                                                                                                                             Bit[7:3]:  Reserved
                               0x5032       ISP_CTRL_32       0x00     RW
                                                                                                                             Bit[2:0]:  dpc_hsize_in[10:8]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 109

                                                                                                                              7-41




      table 7-19    ISP top registers (sheet 3 of 6)

                                              default
        address       register name      value     R/W      description

         0x5033       ISP_CTRL_33       0x00     RW            Bit[7:0]:  dpc_vsize_in[7:0]

                                                                                            Bit[7:2]:  Reserved
         0x5034       ISP_CTRL_34       0x00     RW
                                                                                            Bit[1:0]:  dpc_vsize_in[9:8]

                                                                                            Bit[7:6]:  Reserved
                                                                                               Bit[5]:   r_dpc_sram_rme1
         0x5035       ISP_CTRL_35      0x0F     RW
                                                                                               Bit[4]:    r_dpc_sram_test1
                                                                                            Bit[3:0]:  r_dpc_sram_rm1

                                                                                               Bit[7]:    Test pattern enable
                                                                                               Bit[6]:    Rolling bar function enable
                                                                                               Bit[5]:    Transparent enable
                                                                                               Bit[4]:   Square mode
                                                                                              0:   Gray scale squares
                                                                                              1:   Black-white squares
                                                                                            Bit[3:2]:  Test pattern bar style
                                                                                    00:  Standard test pattern bar
                                                                                    01:  Top-bottom darker test pattern
        0x5E00      PRE CTRL00       0x00     RW                        bar
                                                                                    10:   Right-left darker test pattern
                                                                                      bar
                                                                                         11:  Bottom-top darker test pattern
                                                                                      bar
                                                                                            Bit[1:0]:  Test pattern mode
                                                                                    00:  Test pattern bar
                                                                                    01:  Random data
                                                                                    10:  Square
                                                                                         11:  Black image

                                                                                               Bit[7]:   Reserved
                                                                                               Bit[6]:   Window cut enable        ConfidentialBit[5]:   two_lsb_0_en
                                                                              Set lowest two bits to 0      OmnivisionBit[4]:   Same seed enable
        0x5E01      PRE CTRL01       0x41     RW
                                                                           Reset seed to 0x5E01[3:0] each
                                                                            frame                                                                                            Bit[3:0]: Random seed for
                                                                   Seed used in generating random
                                                                                 data                                                                                            Bit[7:4]:onlyReserved        0x5E02      PRE CTRL02       0x00     RW
                                                                                            Bit[3:0]:  Line number interrupt[11:8]

        0x5E03      PRE CTRL03       0x01     RW            Bit[7:0]:  Line number interrupt[7:0]

        0x5E04      PRE CTRL04       0x00     RW            Bit[7:0]:  Scale x input manual size[15:8]

        0x5E05      PRE CTRL05       0x00     RW            Bit[7:0]:  Scale x input manual size[7:0]

        0x5E06      PRE CTRL06       0x01     RW            Bit[7:0]:  Scale y input manual size[15:8]

        0x5E07      PRE CTRL07       0x00     RW            Bit[7:0]:  Scale y input manual size[7:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 110

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-19    ISP top registers (sheet 4 of 6)

                                                                       default
                             address       register name      value     R/W      description

                                                                                                                             Bit[7:4]:  Reserved
                              0x5E08      PRE CTRL08       0x00     RW
                                                                                                                             Bit[3:0]:  Horizontal manual offset[11:8]

                              0x5E09      PRE CTRL09       0x00     RW            Bit[7:0]:  Horizontal manual offset[7:0]

                                                                                                                             Bit[7:4]:  Reserved
                             0x5E0A      PRE CTRL0A       0x00     RW
                                                                                                                             Bit[3:0]:  Vertical manual offset[11:8]

                             0x5E0B      PRE CTRL0B       0x00     RW            Bit[7:0]:  Vertical manual offset[7:0]

                                                                                                                             Bit[7:4]:  Reserved
                            0x5E0C      PRE RO0C         –        R
                                                                                                                             Bit[3:0]:  Input image pixel number[11:8]

                            0x5E0D      PRE RO0D         –        R               Bit[7:0]:  Input image pixel number[7:0]

                                                                                                                             Bit[7:4]:  Reserved
                             0x5E0E      PRE RO0E         –        R
                                                                                                                             Bit[3:0]:  Input image line number[11:8]

                             0x5E0F      PRE RO0F         –        R               Bit[7:0]:  Input image line number[7:0]

                                                                                                                                 Bit[7]:   Window X offset option
                                                                                                                                 Bit[6]:   Window Y offset option
                                                                                                                                 Bit[5]:   Take first pixel in the same position
                                                                                                                   with no mirror image enable
                                                                                                                                Bit[4]:   Take first pixel in same position with
                                                                                            no flip image enable
                                                                                                                                 Bit[3]:    Mirror option from window
                                                                                                                           0:    First pixel is P2 or P4 with
                                                                                                 window output
                              0x5E10      PRE CTRL10       0x3C      R/W
                                                                                                                           1:    First pixel is P1 or P3 with
                                                                                                 window output
                                                                                                                                 Bit[2]:    Flip option from window
                                                                                                                           0:    First line is P3P4 with window        Confidential      output
                                                                                                                           1:    First line is P1P2 with window      Omnivision                                                                                                                    output
                                                                                                                                 Bit[1]:    Offset manual enable
                                                                                                                                Bit[0]:    Scale size manual enable            for
                                                                                                                                 Bit[7]:   Manual clock/valid ratio enable
                                                                                                                             Bit[6:4]:  Manual dummy line number
                              0x5E11      PRE CTRL11       0x00      R/W            Bit[3]:   Reduce HREF low length by half           onlyBit[2:0]:  Manual clock/valid ratio for dummy
                                                                                                                                    line

                                                                                                                             Bit[7:0]: HREF blank length for dummy
                              0x5E12      PRE RO12         –        R
                                                                                                                              line[15:8]

                                                                                                                             Bit[7:0]: HREF blank length for dummy
                              0x5E13      PRE RO13         –        R
                                                                                                                                    line[7:0]

                              0x5E14      PRE RO14         –        R               Bit[7:0]: HREF length for dummy line[15:8]





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 111

                                                                                                                              7-43




      table 7-19    ISP top registers (sheet 5 of 6)

                                              default
        address       register name      value     R/W      description

        0x5E15      PRE RO15         –        R               Bit[7:0]: HREF length for dummy line[7:0]

                                                                                            Bit[7:5]:  Reserved
                                                                                               Bit[4]:   Dummy error indicating signal
        0x5E16      PRE RO16         –        R
                                                                                               Bit[3]:   Reserved
                                                                                            Bit[2:0]: Dummy line clock ratio output

                                                                                            Bit[7:4]:  Horizontal odd increase step
        0x5E17      PRE RO17         –        R
                                                                                            Bit[3:0]:  Vertical odd increase step

                                                                                            Bit[7:4]:  Reserved
        0x5E18      PRE RO18         –        R
                                                                                            Bit[3:0]:  Horizontal sensor offset[11:8]

        0x5E19      PRE RO19         –        R               Bit[7:0]:  Horizontal sensor offset[7:0]

                                                                                            Bit[7:4]:  Reserved
        0x5E1A      PRE RO1A         –        R
                                                                                            Bit[3:0]:  Vertical sensor offset[11:8]

        0x5E1B      PRE RO1B         –        R               Bit[7:0]:  Vertical sensor offset[7:0]

                                                                                            Bit[7:4]:  Reserved
        0x5E1C      PRE RO1C         –        R
                                                                                            Bit[3:0]:  Horizontal window offset[11:8]

        0x5E1D      PRE RO1D         –        R               Bit[7:0]:  Horizontal window offset[7:0]

                                                                                            Bit[7:4]:  Reserved
        0x5E1E      PRE RO1E         –        R
                                                                                            Bit[3:0]:  Vertical window offset[11:8]

        0x5E1F      PRE RO1F         –        R               Bit[7:0]:  Vertical window offset[7:0]

                                                                                            Bit[7:5]:  Reserved
        0x5E20      PRE RO20         –        R
                                                                                            Bit[4:0]:  Horizontal window output size[12:8]

        0x5E21      PRE RO21         –        R               Bit[7:0]:  Horizontal window output size[7:0]        Confidential
                                                                                            Bit[7:4]:  Reserved
        0x5E22      PRE RO22         –        R      OmnivisionBit[3:0]:  Vertical window output size[11:8]

        0x5E23      PRE RO23         –        R               Bit[7:0]:  Vertical window output size[7:0]                                                                                            Bit[7:6]:  Reserved for
                                                                                            Bit[5:4]:  Horizontal skip
        0x5E24      PRE RO24         –        R
                                                                                            Bit[3:2]:  Reserved
                                                                                            Bit[1:0]:  Vertical skip           only
                                                                                            Bit[7:4]:  Horizontal even increase step
        0x5E25      PRE RO25         –        R
                                                                                            Bit[3:0]:  Vertical even increase step

        0x5E26      RSVD             –          –         Reserved

                                                                                            Bit[7:4]:  Reserved
        0x5E27      PRE RO27         –        R
                                                                                            Bit[3:0]:  Cut top offset for bi-linear BLC[11:8]

        0x5E28      PRE RO28         –        R               Bit[7:0]:  Cut top offset for bi-linear BLC[7:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 112

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-19    ISP top registers (sheet 6 of 6)

                                                                       default
                             address       register name      value     R/W      description

                                                                                                                             Bit[7:4]:  Reserved
                              0x5E29      PRE RO29         –        R               Bit[3:0]:  Cut bottom offset for bi-linear
                                                                                                         BLC[11:8]

                                                                                                                             Bit[7:0]:  Cut bottom offset for bi-linear
                             0x5E2A      PRE RO2A         –        R
                                                                                                         BLC[7:0]

                                                                                                                             Bit[7:4]:  Reserved
                             0x5E2B      PRE CTRL2B       0x09     RW
                                                                                                                             Bit[3:0]:  Array height for bi-linear BLC[11:8]

                            0x5E2C      PRE CTRL2C       0xB0     RW            Bit[7:0]:  Array height for bi-linear BLC[7:0]

                                                                                                                             Bit[7:6]:  Reserved
                            0x5E2D      PRE CTRL2D       0x00     RW             Bit[5]:   Manual horizontal skip enable
                                                                                                                             Bit[4:0]:  Reserved

                                                                                                                             Bit[7:6]:  Reserved
                             0x5E2E      PRE CTRL2E       0x00     RW             Bit[5]:   Manual vertical skip enable
                                                                                                                             Bit[4:0]:  Reserved


                   7.20 window control [0x5A00 ~ 0x5A09, 0x5A10 ~ 0x5A2F]


                           table 7-20    window control registers (sheet 1 of 5)

                                                                           default
                             address     register name           value    R/W     description

                                                                                                                             Bit[7:5]:  Reserved
                                                                                                                              Bit[2:0]:  x_start[10:8]                              0x5A00 ConfidentialXSTART MAN            0x00    RW
                                                                                                     High byte of manual horizontal start      Omnivision                       for manual output window

                                                                                                                              Bit[7:0]:  x_start[7:0]
                              0x5A01     XSTART MAN            0x00    RW              Low byte of manual horizontal start            forfor manual output window

                                                                                                                             Bit[7:4]:  Reserved
                                                                                                                              Bit[1:0]:  y_start[9:8]
                              0x5A02     YSTART MAN            0x00    RW           only High byte of manual vertical start
                                                                                                                                     for manual output window

                                                                                                                              Bit[7:0]:  y_start[7:0]
                              0x5A03     YSTART MAN            0x00     R/W             Low byte of manual vertical start for
                                                                                               manual output window

                                                                                                                             Bit[7:5]:  Reserved
                                                                                                                              Bit[2:0]:  x_window[10:8]
                              0x5A04     XWIN MAN              0x05     R/W
                                                                                                     High byte of manual horizontal
                                                                                             window for manual output window





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 113

                                                                                                                              7-45




      table 7-20    window control registers (sheet 2 of 5)

                                                 default
        address     register name           value    R/W     description

                                                                                             Bit[7:0]:  x_window[7:0]
        0x5A05     XWIN MAN              0x00     R/W             Low byte of manual horizontal
                                                                       window for manual output window

                                                                                             Bit[7:4]:  Reserved
                                                                                             Bit[1:0]:  y_window[9:8]
        0x5A06     YWIN MAN              0x03     R/W
                                                                             High byte of manual vertical
                                                                       window for manual output window

                                                                                             Bit[7:0]:  y_window[7:0]
        0x5A07     YWIN MAN              0x20     R/W             Low byte of manual vertical window
                                                                                                      for manual output window

                                                                                                 Bit[7]:    r_out_size_sel
                                                                                               0:     Initial XY size out
                                                                                               1:   ROI XY size out
                                                                                                 Bit[6]:   r_one_zone_en
                                                                                               0:   16 zones display
                                                                                               1:   Single zone display enable
                                                                                                Bit[5]:   Reserved
                                                                                                Bit[4]:   win16_en
                                                                                               0:      It is not divided into 16 zones
                                                                                               1:   16 zones crop enable
        0x5A08     WINC CTRL             0x86     R/W
                                                                                                Bit[3]:    win_valid_opt
                                                                                                Bit[2]:    emb_flag_sel
                                                                                                Bit[1]:   win_en_opt
                                                                                                Bit[0]:   win_man_en
                                                                        Manual window enable signal
                                                                                               0:   Output window uses system
                                                                                   parameters
                                                                                               1:   Output window uses manual
                                                                                   parameters which are set in        Confidential                                                                                                    register 0x5A00~0x5A07      OmnivisionBit[7:3]:  Reserved
                                                                                             Bit[2:1]:  roi_sel_man
                                                                                     00:  Zone00  [00 01 02 03]
                                                                                     01:  Zone05  [04 05 06 07]            for
                                                                                     10:  Zone10  [08 09 10 11]
                                                                                          11:  Zone15  [12 13 14 15]
        0X5A09     WINC_SEL              0x00     R/W
                                                                                                Bit[0]:   roi_sel_man_en           onlyManual signal zone select
                                                                                               0:   Output zone is selected by
                                                                   GPIO pad
                                                                                               1:   Output zone is selected by
                                                                                      0x5A09[2:1]

                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[2:0]:  win16_xstart0[10:8]
        0x5A10     WIN16 XSTART0         0x00     R/W
                                                                             High byte of first horizontal start for
                                                                          16-window function





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 114

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-20    window control registers (sheet 3 of 5)

                                                                           default
                             address     register name           value    R/W     description

                                                                                                                              Bit[7:0]:  win16_xstart0[7:0]
                              0x5A11     WIN16 XSTART0         0x00     R/W             Low byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[2:0]:  win16_xstart1[10:8]
                              0x5A12     WIN16 XSTART1         0x00     R/W
                                                                                                     High byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                              Bit[7:0]:  win16_xstart1[7:0]
                              0x5A13     WIN16 XSTART1         0x00     R/W             Low byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[2:0]:  win16_xstart2[10:8]
                              0x5A14     WIN16 XSTART2         0x02     R/W
                                                                                                     High byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                              Bit[7:0]:  win16_xstart2[7:0]
                              0x5A15     WIN16 XSTART2         0x80     R/W             Low byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[2:0]:  win16_xstart3[10:8]
                              0x5A16     WIN16 XSTART3         0x03     R/W
                                                                                                     High byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                              Bit[7:0]:  win16_xstart3[7:0]
                              0x5A17     WIN16 XSTART3        0xC0     R/W             Low byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[1:0]:  win16_ystart0[9:8]
                              0x5A18     WIN16 YSTART0         0x00     R/W        Confidential  High byte of first horizontal start for
                                                                                                 16-window function      Omnivision
                                                                                                                              Bit[7:0]:  win16_ystart0[7:0]
                              0x5A19     WIN16 YSTART0         0x00     R/W             Low byte of first horizontal start for
                                                                                                 16-window function            for
                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[1:0]:  win16_ystart1[9:8]
                             0x5A1A     WIN16 YSTART1         0x00     R/W
                                                                                                     High byte of first horizontal start for           only
                                                                                                 16-window function

                                                                                                                              Bit[7:0]:  win16_ystart1[7:0]
                             0x5A1B     WIN16 YSTART1         0x28     R/W             Low byte of first horizontal start for
                                                                                                 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[1:0]:  win16_ystart2[9:8]
                            0x5A1C     WIN16 YSTART2         0x01     R/W
                                                                                                     High byte of first horizontal start for
                                                                                                 16-window function





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 115

                                                                                                                              7-47




      table 7-20    window control registers (sheet 4 of 5)

                                                 default
        address     register name           value    R/W     description

                                                                                             Bit[7:0]:  win16_ystart2[7:0]
        0x5A1D     WIN16 YSTART2         0x90     R/W             Low byte of first horizontal start for
                                                                          16-window function

                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[1:0]:  win16_ystart3[9:8]
        0x5A1E     WIN16 YSTART3         0x01     R/W
                                                                             High byte of first horizontal start for
                                                                          16-window function

                                                                                             Bit[7:0]:  win16_ystart3[7:0]
        0x5A1F     WIN16 YSTART3         0x90     R/W             Low byte of first horizontal start for
                                                                          16-window function

                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[2:0]:  win16_xwindow0[10:8]
        0x5A20     WIN16 XWIN0           0x05     R/W
                                                                             High byte of first horizontal window
                                                                                                      for 16-window function

                                                                                             Bit[7:0]:  win16_xwindow0[7:0]
        0x5A21     WIN16 XWIN0           0x00     R/W             Low byte of first horizontal window
                                                                                                      for 16-window function

                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[2:0]:  win16_xwindow1[10:8]
        0x5A22     WIN16 XWIN1           0x05     R/W
                                                                             High byte of first horizontal window
                                                                                                      for 16-window function

                                                                                             Bit[7:0]:  win16_xwindow1[7:0]
        0x5A23     WIN16 XWIN1           0x00     R/W             Low byte of first horizontal window
                                                                                                      for 16-window function

                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[2:0]:  win16_xwindow2[10:8]
        0x5A24     WIN16 XWIN2           0x02     R/W        ConfidentialHigh byte of first horizontal window
                                                                                                      for 16-window function      Omnivision
                                                                                             Bit[7:0]:  win16_xwindow2[7:0]
        0x5A25     WIN16 XWIN2           0x80     R/W             Low byte of first horizontal window
                                                                                                      for 16-window function            for
                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[2:0]:  win16_xwindow3[10:8]
        0x5A26     WIN16 XWIN3           0x01    RW
                                                                             High byte of first horizontal window           only
                                                                                                      for 16-window function

                                                                                             Bit[7:0]:  win16_xwindow3[7:0]
        0x5A27     WIN16 XWIN3           0x40    RW              Low byte of first horizontal window
                                                                                                      for 16-window function

                                                                                             Bit[7:3]:  Reserved
                                                                                             Bit[1:0]:  win16_ywindow0[9:8]
        0x5A28     WIN16 YWIN0           0x03    RW
                                                                             High byte of first horizontal window
                                                                                                      for 16-window function





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 116

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology




                           table 7-20    window control registers (sheet 5 of 5)

                                                                           default
                             address     register name           value    R/W     description

                                                                                                                              Bit[7:0]:  win16_ywindow0[7:0]
                              0x5A29     WIN16 YWIN0           0x20    RW              Low byte of first horizontal window
                                                                                                                                     for 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[1:0]:  win16_ywindow1[9:8]
                             0x5A2A     WIN16 YWIN1           0x02    RW
                                                                                                     High byte of first horizontal window
                                                                                                                                     for 16-window function

                                                                                                                              Bit[7:0]:  win16_ywindow1[7:0]
                             0x5A2B     WIN16 YWIN1           0xD0    RW              Low byte of first horizontal window
                                                                                                                                     for 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[1:0]:  win16_ywindow2[9:8]
                            0x5A2C     WIN16 YWIN2           0x01    RW
                                                                                                     High byte of first horizontal window
                                                                                                                                     for 16-window function

                                                                                                                              Bit[7:0]:  win16_ywindow2[7:0]
                            0x5A2D     WIN16 YWIN2           0x68    RW              Low byte of first horizontal window
                                                                                                                                     for 16-window function

                                                                                                                             Bit[7:3]:  Reserved
                                                                                                                              Bit[1:0]:  win16_ywindow3[9:8]
                             0x5A2E     WIN16 YWIN3           0x00    RW
                                                                                                     High byte of first horizontal window
                                                                                                                                     for 16-window function

                                                                                                                              Bit[7:0]:  win16_ywindow3[7:0]
                             0x5A2F     WIN16 YWIN3           0xB4    RW              Low byte of first horizontal window
                                                                                                                                     for 16-window function        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 117

                                                                                                                                    8-1


  8  operating specifications

   8.1 absolute maximum ratings


      table 8-1     absolute maximum ratings

         parameter                                                         absolute maximum ratinga

          ambient storage temperature                                            -40°C to +125°C

                                                       VDD-A                 4.5V

          supply voltage (with respect to ground)             VDD-D              3V

                                                           VDD-IO                4.5V

                                               human body model     2000V
            electro-static discharge (ESD)
                                                      machine model        200V

               all input/output voltages (with respect to ground)                              -0.3V to VDD-IO + 1V

           I/O current on any input or output pin                                   ± 200 mA

         peak solder temperature (10 second dwell time)                          245°C

        a.   exceeding the absolute maximum ratings shown above invalidates all AC and DC electrical specifications and may
              result in permanent damage to the device. Exposure to absolute maximum rated conditions for extended periods
         may affect device reliability.


   8.2 functional temperature        Confidential
      table 8-2      functional temperature      Omnivision

         parameter                                               range           operating temperaturea                                          -30°C to +85°C junctionfortemperature
           stable image temperatureb                                    0°C to 50°C junction temperature

        a.   sensor functions but image quality may be noticeably different at temperatures outside of stable image range           only
        b.   image quality remains stable throughout this temperature range





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 118

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    8.3 DC characteristics


                           table 8-3    DC characteristics (TA = 23°C ± 2°C)

                             symbol            parameter                        min        typ      max        unit

                                supply

                                 VDD-A                 supply voltage (analog)                    2.6          2.8          3.0       V

                                   VDD-IO                supply voltage (digital I/O)                 1.7          1.8          3.0       V

                                VDD-D                supply voltage (digital core)               1.14        1.2         1.26      V

                                      IDD-A                                                            24              mA
                                         IDD-IO                   active (operating) currenta                             2.5              mA

                                      IDD-D                                                            52              mA
                                   IDDS-SCCB b                                                        65                 µA
                                                        standby current
                                IDDS-XSHUTDOWN                                                   50                 µA

                                         digital inputs (typical conditions: AVDD = 2.8V, DVDD = 1.5V, DOVDD = 1.8V)

                                       VIL                     input voltage LOW                                               0.54      V

                                   VIH                     input voltage HIGH                      1.26                       V

                                  CIN                     input capacitor                                            10        pF

                                         digital outputs (standard loading 25 pF)

                            VOH                   output voltage HIGH                     1.62                       V

                              VOL                   output voltage LOW                                              0.18      V                                      serial interfaceConfidentialinputs

                                               c               SIOC and SIOD                            -0.5       0           0.54      V      OmnivisionVIL

                                   VIH               SIOC and SIOD                         1.28        1.8          3.0       V

                                      a.   1280x800 @ 120fps            for
                                      b.   with XVCLK

                                        c.   based on DOVDD = 1.8V           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 119

                                                                                                                                    8-3



   8.4 timing characteristics


      table 8-4     timing characteristics

        symbol      parameter                            min        typ      max         unit

           oscillator and clock input

          fOSC          frequency (XVCLK)                        6          24         64        MHz
                    tr, tf           clock input rise/fall time                                          5 (10a)      ns

        a.      if using internal PLL
        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 120

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology

        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 121

                                                                                                                                    9-1


  9  mechanical specifications

   9.1 physical specifications

      figure 9-1    package specifications


                                                                S2
                             1  2  3  4  5  6  7  8            J2     8  7  6  5  4  3  2  1

                                                A                                     A
                                                  B                                      B
                                                  C                                                                                          C    center of BGA (die) =
                                                         W        A
                                               D                                    D    center of the package
                       B                                                  X     D  B
                                                      E                        Z  Y         C      E
                                                        F                                           F
                                               G                                    G
                                               H                                    H

                                                                                             J1                 D                                    A
                            top view                              S1
                                       (bumps down)  C C1 C2 C3 C4 C5 C6       bottom view                                                                                       (bumps up)
                                                                                 note part marking code:
                                                 W - OVT product version
                                                                   X - year part was assembled
                                                                Y - month part was assembled
                            side view                            ZABCD- wafer- lastnumberfour digits of lot number            9281_CSP5_DS_9_1


      table 9-1     package dimensions

         parameter                          symbol     min         typ        max         unit

         package body dimension x             A           5207       5237         5267      µm

         package body dimension y             B           4433       4463         4493      µm

         package height                   C           630        715          800       µm             ball height    ConfidentialC1          100        130          160       µm         package body thickness OmnivisionC2          515        580          645       µm

           thickness from top glass surface to wafer     C3          425        445          465       µm         image plane height                   C4          190        265          340forµm

           glass thickness                      C5          385        400          415       µm

              air gap between sensor and glass         C6          40         45           50       µm           only
             ball diameter                     D           220        250          280       µm

             total ball count                    N                      64 (4 NC)

           pins pitch x-axis                          J1                     640                 µm

           pins pitch y-axis                          J2                     500                 µm

           edge-to-pin center distance along x         S1          346         378.5         411       µm

           edge-to-pin center distance along y         S2          449         481.5         514       µm





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 122

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                    9.2 IR reflow specifications

                            figure 9-2     IR reflow ramp rate requirements


                                    260.0                                                                                                     TP
                                    240.0
note                              220.0                                                             tL            TL
                                                                                               Tmax
The OV9281 uses a                   200.0                                                                                                 reflow
lead free package.
                                    180.0
                                                                 (°C)   160.0                          Tmin
                                                                                                              cooling
                                    140.0                              soaking

                                    120.0                                                                                                                                                                                   temperature   100.0

                                       80.0
                                                 ramp up
                                       60.0

                                       40.0
                                                                                                                                                           Tf
                                       20.0  T0

                                          0.0
                      0 20 40 60 80  100  120  140  160  180  200  220  240  260  280  300  320  340  360  380  400  420  440  460  480  500  520  540

                                                                                  time (sec)                                                                                        9281_CSP5__DS_9_2


                           table 9-2     reflow conditionsab

                            zone                      description                          exposure        Confidential
                              ramp up A (T0 to Tmin)        heating from room temperature to 150°C     temperature slope ≤ 3°C per second      Omnivision
                                   soaking                     heating from 150°C to 200°C             90 ~ 150 seconds

                              ramp up B (tL to Tp)          heating from 217°C to 245°C               temperature slope ≤ 3°C per second            for
                                peak temperature         maximum temperature in SMT            245°C +0/-5°C (duration max 30 sec)

                                      reflow (tL to TL)              temperature higher than 217°C            30 ~ 120 seconds           onlyto 217°C               temperature slope ≤ 3°C per second                              ramp down A (Tp to TL)       cooling from 245°C

                              ramp down B (TL to Tf)       cooling from 217°C to room temperature     temperature slope ≤ 2°C per second

                                 T0 to Tp                 room temperature to peak temperature      ≤ 8 minutes

                                      a.  maximum number of reflow cycles = 3

                                      b.  N2 gas reflow or control O2 gas PPM<500 as recommended





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





   Powered by TCPDF (www.tcpdf.org)

---

## Page 123

                                                                                                                              10-1


  10 optical specifications

   10.1 sensor array center

      figure 10-1   sensor array center

                                          3896 μm



                   A1    A2    A3    A4    A5    A6    A7    A8

                                                                                                               first pixel readout
                                                                                       (2195.5 μm, 1190.5 μm)



                                                                                  package center
                                                                                                (0 μm, 0 μm)        2453 μm
                                                                                              array center
                                                                                          (247.5 μm, -36 μm)


                                  sensor array

                                                          OV9281



                              top view

              note 1 this drawing is not to scale and is for reference only.        Confidential
              note 2 as most optical assemblies invert and mirror the image, the chip is
                                                                                                                                       9281_CSP5_DS_10_1                         typically mountedOmnivisionwith pins A1 to A8 oriented down on the PCB.            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 124

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology



                   10.2 lens chief ray angle (CRA)

                            figure 10-2    chief ray angle (CRA)


                                9


                                8


                                7


                                6


                                5                                                                                                                                                     (degrees)
                                                  CRA  4

                                3


                                2


                                1
                                                                                                CRA

                                0
                                                                                 0.00              0.23              0.45              0.68              0.91              1.13              1.36              1.58              1.81              2.04              2.26

                                                                         image height (mm)                                            9281_DS_10_2


                           table 10-1   CRA versus image height plot                                          field (%)Confidentialimage height (mm)          CRA (degrees)      Omnivision0.00                                0.00                                0.00

                                   0.10                                0.23                                0.90                                   0.20                                0.45  for1.80

                                   0.30                                0.68                                2.70

                                   0.40                                0.91                                3.60           only
                                   0.50                                1.13                                4.50

                                   0.60                                1.36                                5.40

                                   0.70                                1.58                                6.30

                                   0.80                                1.81                                7.20

                                   0.90                                2.04                                8.10

                                   1.00                                2.26                                9.00





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 125

                                                                                                                              10-3



   10.3 IR cut off wavelength


       Wavelength above 975nm must be cut off for both monochrome and color sensors to avoid package structure ghosting.
       For color sensors, it is recommended to cut wavelength at 650nm or shorter for good color reproduction.
        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 126

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology

        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 127

                                                                                                                                          rev-1


 revision history


       version 1.0          07.21.2015

                             •      initial release


       version 1.01         09.18.2015

                             •     in chapter 2, updated figure 2-2


       version 1.02         01.28.2016

                             •     in features, added bullet for DVP parallel output interface

                             •     in key specifications, changed analog active power requirements to 134 µW, standby power
                          requirements to 65 µA, XSHUTDOWN power requirements to 50 µA, added DVP parallel output to
                           output interface specification, added minimum exposure time specification and changed maximum
                        exposure interval specification to maximum exposure time specification

                             •     in chapter 2, changed title of figure 2-2 to "OV9281 MIPI reference schematic", updated figure 2-2,
                      and added figure 2-3 for OV9281 DVP reference schematic

                             •     in table 2-1, added table footnote b, "supports zero row overhead time (ROT) readout

                             •     in table 5-3, added "Minimum exposure time is 1 row period. Maximum exposure time is frame
                           length - 12 row periods, where frame length is set by registers {0x380E, 0x380F}" to description of
                              register 0x3502

                             •     in table 7-5, added "Minimum exposure time is 1 row period. Maximum exposure time is frame
                           length - 12 row periods, where frame length is set by registers {0x380E, 0x380F}" to description of
                              register 0x3502
                             •     in table 8-3, changed typ value for IDD-A to 24mA, changed typ value for IDD-IO to 2.5mA, added
                           IDD-D active current parameter, removed first IDDS-SCCB standby current parameter, changed typ
                                                  to 65µA, and changed typ value of IDDS-XSHUTDOWN to 50µA                          value of IDDS-SCCBConfidential      Omnivision
       version 1.1          05.27.2016

                             •     in key specifications, changed package dimensions to 5237 µm x 4463 µm            for
                             •     in chapter 2, updated figures 2-10 and 2-11

                             •     in table 2-9, changed function for register 0x0301 from PLL1_loop_div to PLL1_multiplier and
                       changed description of bit[1:0] to "PLL1_multiplier[9:8]           only
                             •     in table 2-9, changed function for register 0x0302 from PLL1_loop_div to PLL1_multiplier and
                       changed description of bit[7:0] to "PLL1_multiplier[7:0]

                             •     in table 2-9, changed function for register 0x030C from PLL2_loop_div to PLL2_multiplier and
                       changed description of bit[1:0] to "PLL2_multiplier[9:8]

                             •     in table 2-9, changed function for register 0x030D from PLL2_loop_div to PLL2_multiplier and
                       changed description of bit[7:0] to "PLL2_multiplier[7:0]

                             •     in table 2-9, changed bits used for PLL2_sys_pre_div to bit[1:0]





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 128

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology





                                                                •     in table 2-9, changed function of register 0x0312 from "PLL2_SA1_div" to "PLL2_ana_div" and
                                             changed description of bits[3:0] to "PLL2 analog divider"

                                                                •     in table 2-9, changed function of register 0x0313 from "PLL2_DAC_div" to "PLL2_ADC_div" and
                                             changed description of bits[3:0] to "PLL2 ADC divider"

                                                                •     in table 2-10, changed name of register bits 0x0301[1:0] to PLL1_multiplier[9:8] and register bits
                                                    0x0302[7:0] to PLL1_multiplier[7:0]

                                                                •     in table 2-10, changed name of register bits 0x030C[1:0] to PLL2_multiplier[9:8] and register bits
                                                  0x030D[7:0] to PLL2_multiplier[7:0]

                                                                •     in table 2-10, changed register bits for PLL2_sys_pre_div to 0x030F[3:0]

                                                                •     in table 2-10, changed name of register bits 0x0312[3:0] to PLL2_ana_div

                                                                •     in table 2-10, changed name of register bits 0x0313[3:0] to PLL2_ADC_div

                                                                •     in table 2-11, changed value of PLL_1_multiplier input and PLL_2_multiplier input to 4~27 MHz

                                                                •     in section 4.6, changed second sentence of first paragraph from "The driver uses pad clock input
                                                 from 6~27MHz and..." to "The driver uses pad clock input from 6~64MHz and..."

                                                                •     in table 5-3, changed description of register bits 0x3503[1] and 0x3503[0] to match descriptions
                                           shown in table 7-5

                                                                •     in table 5-3, removed registers 0x350A and 0x350B

                                                                •     in table 5-3, removed "long" from register names and bit descriptions

                                                                •     in table 7-2, changed description of register bits 0x0301[1:0] to "pll1_multiplier[9:8]", changed
                                                        description of register bits 0x0302[7:0] to "pll1_multiplier[7:0]", changed description of register bit
                                                    0x0303[3:0] to "pll1_m_div", changed description of register bits 0x0304[1:0] to "pll1_mipi_div",
                                             changed description of register bits 0x0305[1:0] from "pll1_divsp" to "pll1_sys_pre_div", changed
                                                        description of register bit 0x0306[0] from "pll1_divs" to "pll1_sys_div", changed description of
                                                           register bit 0x030A[0] from "pll1_predivp" to "pll1_pre_div0", changed description of register bits
                                                  0x030C[1:0] to "pll1_multiplier[9:8]", changed description of register bits 0x030D[7:0] to
                                                              "pll1_multiplier[7:0]", changed description of register bits 0x030F[7:4] to "Reserved", changed
                                                        description of register bits 0x030F[3:0] to "pll2_pre_sys_div", changed description of register bits
                                                    0x0312[3:0] to "pll2_ana_div", changed description of register bits 0x0313[3:0] to "pll2_adc_div",
                                             changed description of register bit 0x0314[0] from "pll2_predivp" to "pll2_pre_div0"

                                                                •     in section 7.5, changed title to "...[0x3500 ~ 0x3512, 0x3519 ~ 0x351D]"        Confidential•     in table 7-5, changed description of register bits 0x3505[3:2] to "Debug (always set to 2’b11)"
                                                                •     in table 7-5, changed register 0x3508 to Debug Mode      Omnivision
                                                                •     in table 7-5, changed registers 0x350A~0x3512 to Debug Control and removed rows for registers
                                             0x3513~0x3518

                                                                •     in table 7-5, removed "long" from register names and bit descriptions            for
                                                                •     in table 8-4, changed max value of frequency (XVCLK) from 27MHz to 64MHz

                                                                •     in table 9-1, changed typ and max values for package body dimension x (A) from 5232µm and
                                         5257µm to 5237µm, and 5267µm, respectively           only
                                                                •     in table 9-1, changed typ and max values for package body dimension y (B) from 4458µm and
                                         4483µm to 4463µm, and 4493µm, respectively

                                                                •     in table 9-1, changed min and typ values for package height (C) from 680µm and 740µm to 630µm
                                            and 715µm, respectively

                                                                •     in table 9-1, changed min and typ values for package body thickness (C2) from 575µm and 610µm
                                                           to 515µm and 580µm, respectively

                                                                •     in table 9-1, changed min and typ values for image plane height (C4) from 250µm and 295µm to
                                        190µm and 265µm, respectively





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 129

                                                                                                                                          rev-3





                             •     in table 9-1, changed min and max values for image plane height (C5) from 41µm and 49µm to
                    40µm and 50µm, respectively

                             •     in table 9-1, changed min, typ, and max values for edge-to-pin center distance along x (S1) from
                      351µm, 376µm, and 406µm to 346µm, 378.5µm and 411µm, respectively

                             •     in table 9-1, changed min, typ and max values for edge-to-pin center distance along y (S2) from
                      454µm, 479µm and 509µm to 449µm, 481.5µm, and 514µm, respectively
        Confidential      Omnivision            for           only





     05.27.2016              PRELIMINARY SPECIFICATION               proprietary to OmniVision Technologies





Powered by TCPDF (www.tcpdf.org)

---

## Page 130

  OV9281      b&w CMOS 1 megapixel (1280 x 800) image sensor with OmniPixel3-GS™ technology

        Confidential      Omnivision            for           only





                                 proprietary to OmniVision Technologies              PRELIMINARY SPECIFICATION                version 1.1





Powered by TCPDF (www.tcpdf.org)

---

## Page 131

   defining the future of digital imaging™
        Confidential      Omnivision
  OmniVision Technologies, Inc.            for
  UNITED STATES                       INDIA                       KOREA
    4275 Burton Drive                         Bangalore +91 80 4112 8966             Seoul + 82 2 3472 3769     Santa Clara, CA 95054       only
                                  CHINA                       SINGAPORE +65 6933 1933        tel: +1 408 567 3000
                                                        Beijing + 86 10 6580 1690
       fax: +1 408 567 3001
                                                Shanghai + 86 21 6175 9888       TAIWAN
      email: sales@ovt.com
                                                   +86 21 5774 9288               Taipei +886 2 2657 9800
                                             Shenzhen + 86 755 8384 9733            Hsin-chu +886 3 5656688  UNITED KINGDOM

     Hook, Hampshire +44 1256 744 610      JAPAN

                                           Yokohama +81 45 478 7977
  GERMANY
                                            Osaka +81 6 4964 2606
     Munich +49 89 63 81 99 88



   website: www.ovt.com





Powered by TCPDF (www.tcpdf.org)

---

