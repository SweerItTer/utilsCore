/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-22 15:35:20
 * @FilePath: /EdgeVision/include/utils/mpp/encoderPool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "asyncThreadPool.h"
#include "mpp/encoderCore.h"
#include "mpp/streamWriter.h"

class EncoderPool {
public:
    /**
     * @brief 构造函数
     * @param cfg 编码配置
     * @param core_count 核心数量
     */
    explicit EncoderPool(const MppEncoderContext::Config& cfg, size_t coreCount=4);
    void resetConfig(const MppEncoderContext::Config& cfg);
    /**
     * @brief 析构函数
     */
    ~EncoderPool();
    /**
     * @brief 获取编码核心数量
     * @return size_t 编码核心数量
     */
    size_t coreCount() const { return encoderCores.size(); }
    /**
     * @brief 获取指定核心的指针
     * @param core_id 核心编号
     * @return MppEncoderCore* 指定核心指针, 无效返回 nullptr
     */
    
    bool startRecord(const std::string& outputFile);
    void push_frame();
    void stopRecord();

    bool captureJpeg(const std::string& path);
private:
    const size_t coreCount_ = 0;
    MppEncoderContext::Config cfg_;
    StreamWriter streamWriter_;
    asyncThreadPool corePool;
    std::vector<std::unique_ptr<MppEncoderCore>> encoderCores;
};