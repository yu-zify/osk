# OSK 首次运行配置

编译后直接以普通桌面用户运行 OSK。程序在创建 `/dev/uinput` 设备前自动尝试一次初始化，不必用 root 启动 GUI。

## 会做哪些改动

- 合并 `$XDG_CONFIG_HOME/kwinrulesrc`（通常为 `~/.config/kwinrulesrc`）：仅匹配应用标识 `osk` 且标题为 `OSK 屏幕键盘` 或 `触控板` 的窗口，强制置顶，强制不接受焦点。设置、编辑对话框仍可接受焦点。原有规则会保留，修改前备份到同目录的 `kwinrulesrc.before-osk`；通过会话 D-Bus 请求 KWin 重载。
- 通过 `pkexec` 请求系统授权，安装 `/etc/udev/rules.d/70-osk-uinput.rules`，将 `/dev/uinput` 的组设为 `input`、权限设为 `0660`，添加 `uaccess` 标签。
- 将启动程序的当前普通用户加入 `input` 组。这个组有读取其他输入设备的权限；授权前的说明窗口会提示这一点。
- 安装 `/etc/modules-load.d/osk-uinput.conf`，使下次开机加载 `uinput`；当前会话也会加载模块、重载 udev 规则并触发 `/dev/uinput` 规则。

系统侧需要 `pkexec`（以及桌面 polkit 授权代理）、`getent`、`groupadd`、`usermod`、`modprobe` 和 `udevadm`。这套初始化面向使用 KWin、udev 的常规 Linux 桌面。规则不会保证 OSK 覆盖所有全屏应用；最终层级仍由 KWin 决定。

## 只执行一次

初始化记录独立于外观和游戏布局，保存到 `$XDG_CONFIG_HOME/osk/setup.ini`（通常为 `~/.config/osk/setup.ini`）。在请求系统授权之前先写入 `attempted=true`。授权成功、失败、取消后，后续启动都不会重复自动写规则或弹系统授权。

安装后如果当前会话尚无 `/dev/uinput` 写权限，程序提示注销并重新登录。用户组变更需要新登录会话才能全面生效；`uaccess` 在支持的桌面会话中可能即时授予访问权限。

需要手动重试时删除该初始化记录，再以普通桌面用户启动 OSK。保留原有 `config.ini` 即可保留外观和游戏布局。配置不兼容或写入失败时查看终端的 `[OSK]` 日志；不会自动重复尝试。

## 验证

在 Linux 项目目录执行：

```sh
meson setup build --reconfigure
meson compile -C build
meson test -C build --print-errorlogs
```

首次构建没有 `build` 目录时，用 `meson setup build` 替换第一条命令。测试只在内存中合并示例 KWin 配置，并用 `/bin/sh -n` 解析内置脚本；不会提权、安装规则、访问输入设备或修改用户实际配置。

手动检查：首次启动出现一次说明和系统授权；重启 OSK 不再出现；打开设置后能操作数值输入；触控板和主键盘不抢走目标应用的输入焦点。

KWin 格式参考：[规则设置](https://invent.kde.org/plasma/kwin/-/raw/master/src/rulesettings.kcfg)、[规则索引与迁移](https://invent.kde.org/plasma/kwin/-/raw/master/src/rulebooksettings.cpp)。
