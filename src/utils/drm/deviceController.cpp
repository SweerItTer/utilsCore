/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 19:18:18
 * @FilePath: /src/utils/drm/deviceController.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include <cstring>
#include <libudev.h>
#include<sys/epoll.h>

#include "udevMonitor.h"
#include "drm/deviceController.h"

namespace DrmDev {
    std::mutex fd_mutex;
    DrmDevicePtr fd_ptr = nullptr;  // 全局唯一实例
}

#define FUNCPOINTER

/*对于使用Deleter,存在两种方法*/
#ifdef FUNCPOINTER
/* 方法1: 使用函数指针 
 * 优点: 不需要额外内存开销, 函数指针在编译后是常量, 类型安全
 * 缺点: 只能传递固定类型函数指针, 扩展性不足
 */
template <typename Type, void(*FreeFunc)(Type*)>
struct DrmDeleter {
    void operator()(Type* ptr) const {
        if (ptr) FreeFunc(ptr);
    }
};
#else
/* 方法2: 使用函数对象 
 * 优点: 调用灵活性增强, 允许记录状态
 * 缺点: 需要额外存储可调用对象, 生成的目标代码更大
 */
template <typename Ptr, typename FreeFunc>
struct DrmDeleter {
    FreeFunc free_func;
    
    DrmDeleter(FreeFunc&& func) : free_func(std::forward<FreeFunc>(func)) {}
    
    void operator()(Ptr* ptr) const {
        if (ptr) free_func(ptr);
    }
};
#endif //  FUNCPOINTER

DrmDevicePtr DeviceController::create(const std::string &path)
{
    static std::string path_;
    // 如果已经有实例且路径相同, 直接返回
    if (DrmDev::fd_ptr && path_ == path) {
        return DrmDev::fd_ptr;
    }

    path_ = path;
    const char *node = path_.c_str();
    int fd = ::open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open DRM device: %s : %s\n", node, strerror(errno));
        return nullptr;
    }
    // 检查设备是否可以dump dmabuf
    uint64_t has_dumb = 0;  
    if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 || !has_dumb){
        fprintf(stderr, "drm device '%s' does not support dumb buffers\n",node);
        close(fd);
        return nullptr;
    }
    // 检查是否允许原子提交
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0){
        fprintf(stderr, "drm device '%s' does not support atomic modesetting\n", node);
        close(fd);
        return nullptr;
    }
    
    try{
        auto controller = DrmDevicePtr(new DeviceController(fd));
        if (controller) {
            fprintf(stdout, "Init DeviceController success\n");
            DrmDev::fd_ptr = std::move(controller);
            return DrmDev::fd_ptr;
        } else { 
            fprintf(stderr, "Faild to init DeviceController\n");
            return nullptr;
        }
    } catch (const std::system_error& ex){
        fprintf(stderr, "DmaBuffer::initialize_drm_fd: %s\n",ex.what());
        close(fd);
        return nullptr;
    }
}

DeviceController::~DeviceController()
{
}

DeviceController::DeviceController(int fd) : fd_(fd) {
    // 初始化资源
    refreshResources();
    refreshAllDevices();

    // 在 UdevMonitor 中注册 drm 监听
    UdevMonitor::registerHandler("drm", {"change", "add", "remove"}, 
        std::bind(&DeviceController::handleHotplugEvent, this)
    );
}

void DeviceController::handleHotplugEvent() {
    // static std::atomic_bool changing{false};
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    // if (changing.exchange(true)) return;
    // 通知释放资源 
    notifyPreRefresh();
    // 等待系统稳定(堵塞 UdevMonitor 工作线程)
    // std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    // 更新资源
    refreshResources();
    refreshAllDevices();
    // 重新获取资源
    notifyPostRefresh();
    // changing.store(false);
}

void DeviceController::registerResourceCallback(const ResourceCallback& preRefreshCallback, 
                                                const ResourceCallback& postRefreshCallback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    // 注册回调函数
    callbacks_.emplace_back(preRefreshCallback, postRefreshCallback);
}

// 通知预刷新(释放资源)
void DeviceController::notifyPreRefresh() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (auto& callbackPair : callbacks_) {
        if (callbackPair.first) {
            callbackPair.first();
        }
    }
}
// 通知后刷新(资源再获取)
void DeviceController::notifyPostRefresh() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (auto& callbackPair : callbacks_) {
        if (callbackPair.second) {
            callbackPair.second();
        }
    }
}

std::shared_ptr<drmModeRes> DeviceController::refreshResources() {
    std::lock_guard<std::mutex> fdLock(DrmDev::fd_mutex);
    std::lock_guard<std::mutex> resLock(resMutex_);

    if(fd_.get() < 0){
        fprintf(stderr, "Invalid DRM fd\n");
        return nullptr;
    }

    // 获取临时资源
    drmModeRes* res = drmModeGetResources(fd_.get());
    if (!res) {
        fprintf(stderr, "Failed to get DRM resources: %s\n", strerror(errno));
        return nullptr;
    } 
    // 获取成功后交由shardptr接管
    resources_.reset(res, DrmDeleter<drmModeRes, &drmModeFreeResources>());

    // 刷新plane资源
    int ret = drmSetClientCap(fd_.get(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1); // 若不设置DRM_CLIENT_CAP_UNIVERSAL_PLANES,只会返回Overlay Plane
    if (0 > ret){
        fprintf(stderr, "Failed to set DRM_CLIENT_CAP_UNIVERSAL_PLANES:%d \nOnly allow Overlay Plane\n", ret);
    }
    drmModePlaneRes* planeRes = drmModeGetPlaneResources(fd_.get());
    if (!planeRes) {
        fprintf(stderr, "Failed to get DRM plane resources: %s\n", strerror(errno));
    } else {
        planeResources_.reset(planeRes, DrmDeleter<drmModePlaneRes, &drmModeFreePlaneResources>());  
    }
    return resources_;
}

SharedDev& DeviceController::refreshAllDevices()
{
    std::lock_guard<std::mutex> fdLock(DrmDev::fd_mutex);
    std::lock_guard<std::mutex> resLock(resMutex_);
    std::lock_guard<std::mutex> devLock(devMutex_);
    std::lock_guard<std::mutex> crtcLock(crtcMutex_);


    // 检查 DRM 设备和资源
    if(fd_.get() < 0 || nullptr == resources_){
        fprintf(stderr, "Invalid DRM fd or resources\n");
        return devices_;
    }
    
    // 清空CRTC使用状态
    crtcStatu.clear();

    SharedDev tempDevices;
    for (int i = 0; i < resources_->count_connectors; ++i) {
        // 遍历所有 connector
        drmModeConnectorPtr connector = drmModeGetConnector(fd_.get(), resources_->connectors[i]);
        if ( nullptr == connector ) {
            fprintf(stderr, "Failed to get DRM connector: %s\n", strerror(errno));
            continue;
        }
        // 忽略未连接
        if ( DRM_MODE_CONNECTED != connector->connection ) {
            fprintf(stderr, "Ignoring unused connector: %u\n", connector->connector_id);
            drmModeFreeConnector(connector);
            continue;
        }
        // 忽略无模式
        if ( 0 == connector->count_modes ) {
            fprintf(stderr, "Ignoring connector with no modes: %u\n", connector->connector_id);
            drmModeFreeConnector(connector);
            continue;
        }
        
        // 记录设备信息
        auto devPtr = std::make_shared<drmModeDev>();
        // 初始化 connector
        if (setUpDevice(connector, *devPtr) == 0) { 
            // 将对应的crtc和connector绑定
            if (bindConn2Crtc(fd_.get(), connector->connector_id, devPtr->crtc_id, devPtr->mode) < 0) {
                fprintf(stderr, "Failed to bind connector %u to crtc %u\n", connector->connector_id, devPtr->crtc_id);
                devPtr.reset();
            } else {
                // 入队设备信息
                tempDevices.emplace_back(std::move(devPtr));
            }
        }
        // 释放 connector 资源
        drmModeFreeConnector(connector);
    }
    devices_.clear();
    devices_ = std::move(tempDevices);
    return devices_;
}

int DeviceController::bindConn2Crtc(int fd, uint32_t conn_id, uint32_t crtc_id, drmModeModeInfo& mode)
{   
    uint32_t blob_id;
    int ret = 0;

    // 获取 connector 属性ID
    drmModeObjectPropertiesPtr connProps = drmModeObjectGetProperties(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR);
    if (!connProps) {
        fprintf(stderr, "Failed to get connector properties\n");
        return -1;
    }
	auto property_crtc_id = getPropertyId(fd, connProps, "CRTC_ID");
    drmModeFreeObjectProperties(connProps);
    
    // 获取 CRTC 属性ID
	drmModeObjectPropertiesPtr crtcProps = drmModeObjectGetProperties(fd, crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!crtcProps) {
        fprintf(stderr, "Failed to get connector properties\n");
        return -1;
    }
    auto property_active = getPropertyId(fd, crtcProps, "ACTIVE");
	auto property_mode_id = getPropertyId(fd, crtcProps, "MODE_ID");
    drmModeFreeObjectProperties(crtcProps);
    // 创建模式 Blob // blob 是 DRM 中用于传递复杂数据 (如显示模式) 的机制
	ret = drmModeCreatePropertyBlob(fd, &mode,
				sizeof(mode), &blob_id);

	auto req = drmModeAtomicAlloc();
    if (!req) {
        fprintf(stderr, "Failed to allocate atomic request\n");
        return -1;
    }
	ret |= drmModeAtomicAddProperty(req, crtc_id, property_active, 1);          // 激活 CRTC
	ret |= drmModeAtomicAddProperty(req, crtc_id, property_mode_id, blob_id);   // 设置显示模式
	ret |= drmModeAtomicAddProperty(req, conn_id, property_crtc_id, crtc_id);   // 绑定连接器到 CRTC
	ret |= drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);   // 原子提交

	drmModeAtomicFree(req);
    if (0 > ret) {
        fprintf(stderr, "Failed to bind connector %u to crtc %u: %s\n", conn_id, crtc_id, strerror(-ret));
    } else {
        fprintf(stdout, "Bind connector %u to crtc %u success\n", conn_id, crtc_id);
    }
    return ret;
}

// 寻找可以相互组合的 crtc - connector
int DeviceController::setUpDevice(drmModeConnectorPtr connector, drmModeDev &dev) {
    uint32_t crtc = 0;
    int fd = fd_.get();

    auto mode = connector->modes[0];    // 选择第一个模式作为默认模式
    dev.mode = mode;
    dev.width = mode.hdisplay;
    dev.height = mode.vdisplay;
    dev.connector_id = connector->connector_id;

    fprintf(stdout, "Mode for connector %u is %ux%u\n", 
        connector->connector_id, dev.width, dev.height);
    
    // 查询当前活动的encoder 和 crtc
    drmModeEncoderPtr encoder = drmModeGetEncoder(fd, connector->encoder_id);
    if ( nullptr != encoder ) {
        crtc = encoder->crtc_id;
    }
    // 释放当前 encoder 资源
    drmModeFreeEncoder(encoder);
    encoder = nullptr;
    
    auto it = crtcStatu.find(crtc); // 查询 CRTC 使用状态
    // 未记录, 并且已连接 CRTC
    if ( it == crtcStatu.end() && 0 != crtc ) {
        // 初始化为占用
        crtcStatu[crtc] = true;
        // 备份旧配置
        dev.oldCrtc = drmModeGetCrtc(fd, crtc);
        dev.crtc_id = crtc;
        fprintf(stdout, "Find CRTC %u for connector %u\n", crtc, connector->connector_id);
        return 0;
    }

    // 若未连接CRTC或已被占用
    for (int i = 0; i < connector->count_encoders; ++i) {
        // 遍历所有连接的编码器
		encoder = drmModeGetEncoder(fd, connector->encoders[i]);
		if (nullptr == encoder) {
			fprintf(stderr, "Cannot retrieve encoder %u:%u (%d): %m\n",
				i, connector->encoders[i], errno);
			continue;
		}

		// 迭代全局CRTC
		for (int j = 0; j < resources_->count_crtcs; ++j) {
			// CRTC是否与编码器一起工作
			if (!(encoder->possible_crtcs & (1 << j)))
				continue;

			// CRTC是否被占用
			crtc = resources_->crtcs[j];
			if (crtcStatu.find(crtc) != crtcStatu.end() && true == crtcStatu[crtc]) {
                continue;
            }
            // 找到可用CRTC
			crtcStatu[crtc] = true; // 标记为占用
            dev.crtc_id = crtc;
            fprintf(stdout, "Find CRTC %u for connector %u\n", crtc, connector->connector_id);
            
            drmModeFreeEncoder(encoder);
            encoder = nullptr;
            return 0;
		}
		drmModeFreeEncoder(encoder);
        encoder = nullptr;
	}

    fprintf(stderr, "Failed to setup device for connector %u, cannot find suitable CRTC\n", connector->connector_id);
	return -1;
}

bool DeviceController::isFormatSupported(uint32_t format, drmModePlanePtr plane) const
{
    if ((nullptr == planeResources_) || (0 > fd_.get())) {
        return false;
    }

    if (nullptr == plane) {
        return false;
    }
    if (0 == format){
        fprintf(stdout, "Plane %u\n",plane->plane_id);
    }
    for (uint32_t i = 0; i < plane->count_formats; ++i) {
        auto planeFormat = plane->formats[i];
        if (0 == format) {
            fprintf(stdout, "\tSupported format: %s \n",
                    fourccToString(planeFormat).c_str());
            continue;
        } 
        if (planeFormat == format) {
            fprintf(stdout, "Find matched format: %s for plane %u\n",
                    fourccToString(planeFormat).c_str(), plane->plane_id);
            return true;
        }
    }
    fprintf(stdout, "\n");
    return (0 != format) ? false : true; // 当 format==0 仅列出不做判断
}

int DeviceController::getPlaneType(uint32_t plane_id)
{
    // 根据ID查询PLANE属性
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
        fd_.get(), plane_id, DRM_MODE_OBJECT_PLANE);
    if (nullptr == props) {
        return -1;
    }

    int type = -1;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        // 获取属性
        drmModePropertyPtr prop = drmModeGetProperty(fd_.get(), props->props[i]);
        if (nullptr != prop) {
            // 属性为type
            if (0 == strcmp(prop->name, "type")) {
                type = static_cast<int>(props->prop_values[i]);
                drmModeFreeProperty(prop);
                break;
            }
            drmModeFreeProperty(prop);
        }
    }
    // 释放资源
    drmModeFreeObjectProperties(props);
    return type;
}

size_t DeviceController::refreshPlane(uint32_t crtc_id)
{
    std::lock_guard<std::mutex> fdLock(DrmDev::fd_mutex);
    std::lock_guard<std::mutex> resLock(resMutex_);
    std::lock_guard<std::mutex> CacheLock(cacheMutex_);
    
    if (fd_.get() < 0) { // fd 有可能为0,虽然大概率是被stdin占用
        return {};
    }
    if (!planeResources_) {
        fprintf(stderr, "No planeResources found.\n");
        return {};
    }
    // 实际上drm没有给出过任何说明crtcid不为0,其为任意值,不能它通过==0来判断有效性

    std::vector<uint32_t> planeIDs;
    planesCache_.clear();
    auto count_planes = planeResources_->count_planes;
    fprintf(stdout, "Find %zu planes in resources.\n", count_planes);

    for (uint32_t i = 0; i < count_planes; i++) {
        // 遍历plane
        uint32_t plane_id = planeResources_->planes[i];
        drmModePlanePtr plane = drmModeGetPlane(fd_.get(), plane_id);
        
        if (!plane) continue;
        
        // 检查 Plane 是否支持指定的 CRTC
        bool supports_crtc = false;
        for (int j = 0; j < resources_->count_crtcs; j++) {
            // 检查crtc有效性
            if (resources_->crtcs[j] != crtc_id) {
                fprintf(stdout, "Plane %u do not match crtc %u\n", plane_id, crtc_id);
            } else {
                supports_crtc = (plane->possible_crtcs & (1 << j));
                break;
            }
        }
        // 检查是否支持指定格式
        if (supports_crtc) {
            planeIDs.emplace_back(plane_id);

            PlanesCachePtr cache = std::make_shared<PlanesPropertyCache>();
            cache->plane = plane;                    // 直接缓存plane
            cache->type = getPlaneType(plane_id);    // 查询plane类型
            // 区间(所有格式)
            cache->formats.assign(plane->formats, plane->formats + plane->count_formats);

            planesCache_[plane_id] = std::move(cache);
        } else {// 不支持 CRTC 或 不支持格式
            drmModeFreePlane(plane);
        }
    }
    if (planeIDs.empty()) fprintf(stdout, "No plane matched. Fxxk\n");
    // 返回planeID列表
    return planeIDs.size();
}

void DeviceController::getPossiblePlane(int plane_type, uint32_t format, std::vector<uint32_t>& out_ids) {
    if (planesCache_.size() == 0) {
        fprintf(stdout, "There is no plane cached.\n");
        return;
    }
    out_ids.clear();
    std::lock_guard<std::mutex> lock(cacheMutex_);
    // 筛选指定类型plane
    for (const auto &pair : planesCache_) {
        // 不支持plane类型
        if (pair.second->type != plane_type) continue;
        // 不支持对应格式
        if (false == isFormatSupported(format, pair.second->plane)) continue;
        out_ids.push_back(pair.first);
    }
}

uint32_t DeviceController::getPropertyId(int fd, drmModeObjectPropertiesPtr props, const char *name)
{
    drmModePropertyPtr property;
	uint32_t id = 0;

	for (uint32_t i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!strcmp(property->name, name))
			id = property->prop_id;
		drmModeFreeProperty(property);

		if (id) break;
	}

	return id;
}
