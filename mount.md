### Ubuntu
```bash
sudo apt-get install nfs-kernel-server -y
cd /
mkdir nfs
chmod 777 nfs

sudo echo "/nfs 192.168.*(rw,sync,no_root_squash,no_subtree_check)" >> /etc/exports
sudo service portmap restart
sudo service nfs-kernel-server restart
showmount -e
```

---

### Board
```bash
cd /
mkdir nfs

mount -t nfs -o nolock <Ubuntu IP>:/nfs  /nfs
sync
```

[rgademo](../../../RK3568/SDK/atk-rk3568_linux_release_v1.4_20250104/external/linux-rga/samples/im2d_api_demo/rgaImDemo.cpp)