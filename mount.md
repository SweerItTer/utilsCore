# 目录

- [WSL2](#wsl2)
  - [安装 NFS 相关服务](#安装-nfs-相关服务)
  - [创建共享文件夹](#创建共享文件夹)
  - [修改文件夹权限](#修改文件夹权限)
  - [编辑/etc/exports以挂载文件夹](#编辑/etc/exports以挂载文件夹)
  - [调整防火墙](#调整防火墙)
  - [重启NFS服务并开启rpcbind](#重启NFS服务并开启rpcbind)
- [Windows](#windows)
  - [PowerShell 自定义配置](#powershell-自定义配置)
  - [监听端口](#监听端口)
  - [开启防火墙](#开启防火墙)
- [外部设备](#外部设备)
  - [挂载文件系统](#挂载文件系统)
- [测试](#测试)
- [尾声和补充](#尾声和补充)

---
# WSL2

### 安装 NFS 相关服务

安装nfs-kernel-server和rpcbind
```bash
sudo apt-get install nfs-kernel-server rpcbind
```

检查服务启动状态
```bash
systemctl status nfs-kernel-server
```
 
---
### 创建共享文件夹

```bash
sudo mkdir /mnt/nfs
```
---
### 修改文件夹权限

修改拥有者和组
```bash
sudo chown nobody:nogroup /mnt/nfs
```
运行任何人访问
```bash
sudo chmod 777 /mnt/nfs
```
---
### 编辑/etc/exports以挂载文件夹
添加内容:
```ini
/mnt/nfs *(rw,sync,no_root_squash,no_subtree_check)
```
检查配置文件`/etc/export`是否有错误
```bash
sudo exportfs -ar
```
使启动立即生效
```bash
sudo exportfs -rv 
```
检查是否挂载成功
```bash
showmount -e
```

---
### 调整防火墙
指定信任的IP
```bash
sudo ufw allow from [Client IP] to any port nfs
```
指定网段
比如:
```bash
sudo ufw allow from 192.168.1.0/24 to any port nfs
```
允许任何人
```bash
sudo ufw allow from any to any port nfs
```
或者
```bash
sudo ufw allow to any port nfs
```

---
### 重启NFS服务并开启rpcbind

```bash
sudo /etc/init.d/nfs-kernel-server restart
sudo service rpcbind start
```

---

# Windows

### PowerShell 自定义配置
启动用`powershell`运行以下命令查看文件配置文件所在路径
```powershell
$PROFILE
```
在该位置新建任意名字的`.ps1`文件
并写入如下
```powershell
# 获取 WSL 的 IP 地址
$WSLIP = $(wsl hostname -I).Split(" ")[0].Trim()

# 添加 WSL 端口转发和防火墙规则
function Add-WSLPort ($Port = '23333', $Protocol = 'TCP') {
    netsh interface portproxy add v4tov4 listenport=$Port connectaddress=$WSLIP connectport=$Port protocol=$Protocol
    New-NetFirewallRule -DisplayName "Allow ${Protocol} Inbound Port ${Port}" -Direction Inbound -Action Allow -Protocol $Protocol -LocalPort $Port
}

# 移除 WSL 端口转发和防火墙规则
function Remove-WSLPort ($Port = '23333', $Protocol = 'TCP') {
    netsh interface portproxy delete v4tov4 listenport=$Port protocol=$Protocol
    Get-NetFirewallRule -DisplayName "Allow ${Protocol} Inbound Port ${Port}" | Remove-NetFirewallRule
}
```

---
### 监听端口
用管理员权限启动`powershell`
运行命令开启监听
```powershell
Add-WSLPortForwarding 2049
Add-WSLPortForwarding 443
```
查看状态
```powershell
netsh interface portproxy show v4tov4
```
---
### 开启防火墙
依然在之前的窗口
添加入站规则:允许`443`,`2049`端口
```powershell
New-NetFirewallRule -DisplayName "WSL NFS TCP" -Direction Inbound -LocalPort 443,2049 -Protocol TCP -Action Allow
```
---

# 外部设备

### 挂载文件系统
创建文件夹用于挂载远程文件系统
```zsh
sudo mkdir ~/nfs_share
```
挂载
```zsh
sudo mount <IP>:/mnt/nfs ~/nfs_share
```
命令原型:
```zsh
sudo mount [Server IP]:[Path to shared] [mount point]
```
---

# 测试
- 外部设备
```zsh
cd ~/nfs_share
touch test.txt
echo "Hi" > ./test.txt
sync
```
- WSL
```bahs
ls /mnt/nfs/test.txt # 如果没有文件可能需要sync同步试试
cat /mnt/nfs/test.txt
Hi
```

---
# 尾声和补充

#### 关于删除防火墙规则

先检查是否存在
```powershell
Get-NetFirewallRule -DisplayName "WSL NFS *"
```
删除
```powershell
Remove-NetFirewallRule -DisplayName "WSL NFS TCP"
```


---

参考资料:
[WSL2：实现客户端和服务端之间的NFS服务](https://blog.csdn.net/Tea_Char/article/details/130020600)
[WSL 2 网络配置](https://blog.csdn.net/Yiang0/article/details/127780263)