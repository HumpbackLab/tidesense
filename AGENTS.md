本项目是SG2002 的SDK

我们的产品叫做TideSense， 是一款基于SG2002的双目VIO，使用SG2002的arm A53 cpu.

摄像头使用ov9281，双目基线是50mm，板载sc7u22的IMU。

Soc的引脚分配见 Pin.md

相关的手册见docs目录。

如果要替换内核，可以登录到设备上，使用dd来烧录新内核。
用户名root，密码cvitek

设备IP：192.168.123.99

左边摄像头为：0x10(7位地址)、2lane
右边摄像头为：0x60(7位地址)、1lane



