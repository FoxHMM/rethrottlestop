# AUR 发布说明

## 生成源码包

```bash
cd "/path/to/ReThrottleStop"
git archive --format=tar.gz --prefix=rethrottlestop-1.0.0/ \
    -o packaging/rethrottlestop-1.0.0.tar.gz HEAD
sha256sum packaging/rethrottlestop-1.0.0.tar.gz
```

把得到的 sha256 填入 PKGBUILD 的 `sha256sums=()`。

## 本地构建测试

```bash
cd packaging
makepkg -f          # 生成 .pkg.tar.zst
makepkg --printsrcinfo > .SRCINFO
sudo pacman -U rethrottlestop-1.0.0-1-x86_64.pkg.tar.zst
```

## 提交到 AUR

```bash
git clone ssh://aur@aur.archlinux.org/rethrottlestop.git aur-rethrottlestop
cd aur-rethrottlestop
cp ../packaging/PKGBUILD ../packaging/.SRCINFO .
git add PKGBUILD .SRCINFO
git commit -m "Initial import: rethrottlestop 1.0.0-1"
git push
```

## 包结构

```
usr/bin/rethrottlestop
usr/lib/systemd/system/rethrottlestop-daemon.service
usr/lib/udev/rules.d/99-rethrottlestop.rules
usr/share/applications/rethrottlestop.desktop
usr/share/doc/rethrottlestop/{README,TODO}.md
usr/share/licenses/rethrottlestop/LICENSE
```

## 安装后

```bash
sudo modprobe msr
sudo systemctl enable --now rethrottlestop-daemon.service
rethrottlestop
```

GUI 进程无任何特权，所有 MSR 读写由守护进程代劳。
