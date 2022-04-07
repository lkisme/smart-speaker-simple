# smart-speaker-simple
20多块钱可以实现一个自定义唤醒词、最多150条命令词的智能语音助手。所需硬件：
1. SU-03T（10.5块）
2. ESP01S（9块，不要买便宜货，要买安信可正版的）
3. 咪头（0.5块）
4. 喇叭（2块）
通过本程序，结合HomeAssistant、nodered（可选，可以直接使用HomeAssistant的自动化实现），可以控制家里的智能设备。再也不用担心家里的智能音箱会“监听”自己了。

## 本程序功能
1. 读取SU-03T的串口输出，并转发到MQTT
2. 监听MQTT，并发送到SU-03T
3. OTA更新
4. 通过HTTP修改设备名称
5. 通过HTTP修改WiFi密码
6. 记录并输出crash log
7. 结合Home Assistant查看信息


> 不支持light sleep，因为light sleep会导致8266掉线

## 代码需要修改的地方
1. MQTT server地址
2. WiFi的ssid和密码（可以通过http命令修改）
3. 如果需要发送指令给SU-03T，需要从平台上下载相应的代码，然后做一些小修改