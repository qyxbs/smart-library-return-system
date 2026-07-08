# K230立创庐山派开发完整指南

## 1. 硬件准备

在开始开发前，请确保您拥有以下硬件和配件：

### 必需硬件
- 立创庐山派K230开发板 (型号: LCKFB-K230)
- Micro USB线 (用于供电和串口调试)
- Type-C USB线 (用于烧录固件)
- SD卡 (可选，用于SD卡烧录方式)
- 电源适配器 (5V/2A推荐)

### 可选配件
- 音频输入设备 (如麦克风或PDM麦克风阵列)
- 串口调试工具 (CH340等USB转串口模块)
- 杜邦线若干

## 2. 开发环境搭建

### 2.1 系统要求
- 操作系统: Ubuntu 20.04 LTS (x86_64) 推荐
- 内存: 建议 ≥ 4GB (编译过程需要大量内存)
- 磁盘空间: ≥ 15GB 可用空间
- 网络: 稳定的互联网连接 (用于代码同步和工具链下载)

### 2.2 安装系统依赖

```bash
# 1. 更新APT源 (推荐使用国内源加速)
sudo cp /etc/apt/sources.list /etc/apt/sources_bak.list
sudo sed -i "s/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list
sudo sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list
sudo apt update

# 2. 安装基础开发工具
sudo dpkg --add-architecture i386
sudo apt update

sudo apt install -y --no-install-recommends \
    sudo vim wget curl git git-lfs openssh-client net-tools sed tzdata expect \
    make cmake binutils build-essential gcc g++ bash patch perl tar cpio unzip \
    file bc bison flex autoconf automake python3 python3-pip python3-dev \
    lib32z1 libncurses5-dev fakeroot pigz tree doxygen gawk pkg-config \
    libssl-dev libc6-dev-i386 libncurses5:i386 libconfuse-dev python-is-python3 scons libyaml-dev mtools

# 清理缓存
sudo apt clean
```

### 2.3 配置Python环境

```bash
# 配置PIP国内镜像源
pip3 config set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple

# 安装Python依赖库
pip3 install -U pyyaml pycryptodome gmssl jsonschema jinja2
```

### 2.4 安装repo工具

```bash
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+rx ~/.bin/repo
echo 'export PATH="$HOME/.bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

## 3. 下载K230 RTOS SDK

### 3.1 配置Git账户
```bash
# 配置你的Git账户信息
git config --global user.email "your_email@example.com"
git config --global user.name "Your Name"

# 如果使用SSH方式，需要配置SSH密钥
ssh-keygen -t rsa -C "your_email@example.com"
# 然后将公钥添加到Gitee或GitHub账户
```

### 3.2 下载SDK源码
```bash
# 创建工作目录
mkdir -p ~/rtos_k230 && cd ~/rtos_k230

# 从Gitee下载代码 (推荐国内用户)
repo init -u git@gitee.com:canmv-k230/manifest.git --repo-url=git@gitee.com:canmv-k230/git-repo.git

# 如果上面的命令失败，尝试HTTP方式
# repo init -u https://gitee.com/canmv-k230/manifest.git --repo-url=https://gitee.com/canmv-k230/git-repo.git

# 同步代码 (这可能需要较长时间，请耐心等待)
repo sync -j $(nproc) --no-clone-bundle
```

### 3.3 下载工具链
```bash
# 进入SDK目录
cd ~/rtos_k230

# 首次编译需要下载工具链 (仅需执行一次)
make dl_toolchain
```

## 4. 选择立创庐山派配置

### 4.1 查看支持的配置
```bash
cd ~/rtos_k230
make list-def
```

查找与立创庐山派相关的配置，通常会有 `k230_rtos_lckfb_defconfig`。

### 4.2 选择立创庐山派配置
```bash
# 如果有专门的RT-Smart配置
make k230_rtos_lckfb_defconfig

# 如果只有CanMV配置，则使用CanMV配置然后禁用CanMV启用RTOS
# make k230_canmv_lckfb_defconfig
# make menuconfig  # 然后在菜单中关闭CanMV组件，启用RTOS组件
```

### 4.3 (可选) 自定义配置
如果需要启用特定功能，可以使用menuconfig：
```bash
make menuconfig
```

在菜单中可以：
- 配置音频输入相关功能
- 启用/禁用特定的示例程序
- 调整系统参数

## 5. 编译固件

### 5.1 开始编译
```bash
cd ~/rtos_k230
# 开始完整编译 (这可能需要较长时间)
time make log
```

### 5.2 编译结果
编译成功后，固件镜像将位于：
```bash
~/rtos_k230/output/k230_rtos_lckfb_defconfig/images/
```

主要文件包括：
- `k230_canmv_lckfb.kdimg` - 用于K230BurningTool的镜像
- `k230_canmv_lckfb.img` - 用于SD卡烧录的镜像

## 6. 烧录固件到开发板

### 6.1 使用K230BurningTool (USB烧录方式) - 推荐

#### 6.1.1 安装烧录工具
- 下载地址: https://www.kendryte.com/en/resource?selected=0-2-2
- 下载K230BurningTool
- 解压并运行

#### 6.1.2 进入烧录模式
将开发板进入烧录模式，有以下几种方式：

**方式1: 默认启动介质无有效固件**
- 对于新购买的开发板，或清空EMMC/SD卡后的开发板

**方式2: 强制切换启动模式**
- 按住开发板上的Boot按键
- 保持按住的同时给开发板上电
- 继续按住几秒钟直到PC检测到设备

**方式3: 终端命令触发**
- 如果系统已经运行，可在RtSmart终端执行:
```bash
reboot_to_upgrade
```

#### 6.1.3 使用K230BurningTool烧录
1. 连接开发板到PC (Type-C USB线)
2. 打开K230BurningTool
3. 确认工具检测到设备
4. 选择固件文件 (`*.kdimg`)
5. 点击"开始烧录"
6. 等待烧录完成

### 6.2 使用SD卡烧录方式

#### 6.2.1 准备SD卡
- 准备一张≥8GB的SD卡
- 使用SD卡读卡器连接到PC

#### 6.2.2 烧录工具
使用Rufus或balenaEtcher:
- Rufus: https://rufus.ie/en/
- balenaEtcher: https://etcher.balena.io/

#### 6.2.3 烧录步骤
1. 将SD卡插入读卡器
2. 打开烧录工具 (Rufus或balenaEtcher)
3. 选择镜像文件 (`*.img`，不是`.kdimg`)
4. 选择SD卡作为目标设备
5. 开始烧录
6. 烧录完成后将SD卡插入开发板
7. 从SD卡启动开发板

## 7. 连接串口查看日志

### 7.1 连接串口
1. 使用Micro USB线连接开发板的调试串口到PC
2. 确认设备识别:
```bash
dmesg | tail
# 应该看到类似: ttyUSB0 or ttyACM0
```

### 7.2 安装串口工具
```bash
sudo apt install screen minicom
```

### 7.3 连接串口
```bash
# 使用screen (推荐)
screen /dev/ttyUSB0 115200

# 或使用minicom
minicom -D /dev/ttyUSB0 -b 115200
```

### 7.4 退出串口连接
- 在screen中: 按 `Ctrl+A` 然后按 `K` 然后按 `Y` 确认
- 或者直接断开USB连接

## 8. 添加音频采集程序

### 8.1 修改编译配置
将自定义应用程序添加到SDK的应用目录中。

```bash
# 复制应用程序到SDK应用目录
cp your_application.c ~/rtos_k230/src/rtsmart/examples/
```

### 8.2 创建应用目录结构
```bash
mkdir -p ~/rtos_k230/src/rtsmart/examples/audio_capture
```

### 8.3 创建Makefile
```bash
cat > ~/rtos_k230/src/rtsmart/examples/audio_capture/Makefile << 'EOF'
include $(TOP_DIR)/config.mk
include $(TOP_DIR)/build/common.mk

LIBS-y += -lm -lpthread

ifeq (${CONFIG_SDK_ENABLE_RTOS_AWA}, y)
CFLAGS += -DCONFIG_AWA_ENABLE
endif

SRCS := $(wildcard *.c)
TARGET := audio_capture.elf

$(eval $(call k510_std_app,$(TARGET),$(SRCS),$(LIBS-y)))

.PHONY: all clean

all: $(TARGET)

clean:
	rm -f *.o $(TARGET)
EOF
```

### 8.4 在菜单配置中添加选项
需要修改示例程序的配置文件以包含音频采集示例。

## 9. 运行音频采集程序

### 9.1 重新编译包含音频采集程序的固件
```bash
cd ~/rtos_k230
make k230_rtos_lckfb_defconfig
time make log
```

### 9.2 烧录新固件
使用之前介绍的烧录方法将新固件烧录到开发板。

### 9.3 运行音频采集程序
1. 通过串口连接开发板
2. 等待系统完全启动
3. 运行音频采集程序:

```bash
cd /sdcard/app/examples
ls -la  # 查看可用示例
./audio_capture/audio_capture.elf
```

### 9.4 测试音频采集
程序启动后会显示菜单:
- 输入 `start` 开始音频采集
- 输入 `stop` 停止音频采集
- 输入 `status` 查看当前状态
- 输入 `quit` 退出程序

## 10. 调试和故障排除

### 10.1 常见问题解决

**Q: 编译时提示工具链未找到**
A: 确保已执行 `make dl_toolchain`，检查 `~/.kendryte/k230_toolchains` 目录

**Q: 烧录工具无法检测到设备**
A: 确保开发板正确进入烧录模式，检查USB连接

**Q: 串口无输出**
A: 确认连接的是调试串口，检查波特率设置(115200)

**Q: 音频采集无声**
A: 检查麦克风连接，确认音频接口配置正确

### 10.2 日志调试
```bash
# 查看系统日志
dmesg

# 查看特定进程日志
ps | grep audio
```

### 10.3 性能监控
在串口终端可以查看系统的实时状态和性能信息。

## 11. 附录：常用命令汇总

### 11.1 开发命令
```bash
# 环境初始化
mkdir -p ~/rtos_k230 && cd ~/rtos_k230
repo init -u git@gitee.com:canmv-k230/manifest.git --repo-url=git@gitee.com:canmv-k230/git-repo.git
repo sync -j $(nproc)

# 工具链下载
make dl_toolchain

# 配置和编译
make k230_rtos_lckfb_defconfig
time make log

# 查看可用配置
make list-def
```

### 11.2 运行时命令
```bash
# 在开发板串口终端
cd /sdcard/app/examples
ls -la
./audio_capture/audio_capture.elf

# 系统重启到升级模式
reboot_to_upgrade
```

### 11.3 串口连接
```bash
# 连接串口
screen /dev/ttyUSB0 115200
```

此指南提供了从零开始开发K230立创庐山派项目的完整流程，包括环境搭建、代码编译、固件烧录和程序运行等所有必要步骤。