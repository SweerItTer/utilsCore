#include "postprocess.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <cstdio>

namespace postprocess {

bool read_class_names(const std::string& path, std::vector<std::string>& class_names) {
    class_names.clear();
    std::ifstream infile(path);
    if (!infile.is_open()) {
        fprintf(stderr, "[postprocess] failed to open class file: %s\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty()) class_names.push_back(line);
    }
    infile.close();
    fprintf(stdout, "[postprocess] loaded %zu class names\n", class_names.size());
    return true;
}

static inline float iou(const rect_pos& a, const rect_pos& b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.w, b.x + b.w);
    float y2 = std::min(a.y + a.h, b.y + b.h);
    float inter_w = std::max(0.0f, x2 - x1);
    float inter_h = std::max(0.0f, y2 - y1);    // 获取重叠的框(对比两个box)

    float inter = inter_w * inter_h;            // 获取交集面积
    float uni = a.w * a.h + b.w * b.h - inter;  // 计算并集面积
    return inter / uni; // 返回 IoU
}

// ------------------------------------------------------------
// 快速NMS: 保留概率最高的框
// ------------------------------------------------------------
static void nms_fast(
    const std::vector<rect_pos>& boxes,
    const std::vector<float>& scores,
    float iou_thresh,
    std::vector<int>& keep_idx
) {
    std::vector<int> idxs(boxes.size());
    // 生成索引 0~boxes.size()
    std::iota(idxs.begin(), idxs.end(), 0);
    // 根据box概率排序
    std::sort(idxs.begin(), idxs.end(),
        [&](int a, int b) { return scores[a] > scores[b]; });

    keep_idx.clear();
    for (int i : idxs) {
        bool keep = true;
        for (int j : keep_idx) {
            // IoU 筛选
            if (iou(boxes[i], boxes[j]) > iou_thresh) {
                keep = false;
                break;
            }
        }
        // 入队保留的box
        if (keep) keep_idx.push_back(i);
    }
}

// ------------------------------------------------------------
// 每层处理 (模板) - 解码 YOLO 输出为 bbox + score + class_id 
// RKNN 输出格式: (1, 255, H, W) = (1, 3*85, H, W)
// 内存布局: NCHW - [C0的HW数据, C1的HW数据, ..., C254的HW数据]
// 每85个通道对应一个anchor: [anchor0的85通道, anchor1的85通道, anchor2的85通道]
// ------------------------------------------------------------
template <typename T>
static void process_layer_rule(
    T* input,                    // 当前输出层数据
    int grid_h, int grid_w,      // 特征层尺寸
    int stride,                  // 下采样步幅
    int num_classes,             // 类别数量
    float conf_thresh,           // 置信度阈值
    int zp, float scale,         // 量化参数
    const AnchorLayer& anchors,  // 当前层 anchor
    std::vector<rect_pos>& out_boxes,
    std::vector<float>& out_scores,
    std::vector<int>& out_class_ids
) {
    const int grid_len = grid_h * grid_w;
    const int anchor_num = anchors.size();  // 通常是 3
    const int prop_per_anchor = num_classes + 5; // 85 = 5 + 80

    // lambda: 量化反算
    auto dequant = [&](T v) -> float {
        if (std::is_same<T, int8_t>::value) {
            return static_cast<float>((v - zp) * scale);
        }
        return static_cast<float>(v);
    };

    // 量化后的阈值
    T thres_quantized;
    if (std::is_same<T, int8_t>::value) {
        float dst_val = (conf_thresh / scale) + zp;
        thres_quantized = static_cast<T>(std::max(-128.0f, std::min(127.0f, dst_val)));
    }

    // ------------------------------------------------------------
    // NCHW 格式遍历
    // 对于输出 (1, 255, H, W): 
    // - 前85个通道 (0-84) 是 anchor0 的数据
    // - 接下来85个通道 (85-169) 是 anchor1 的数据  
    // - 最后85个通道 (170-254) 是 anchor2 的数据
    // 每个通道存储 H×W 个网格的同一属性值
    // ------------------------------------------------------------
    for (int a = 0; a < anchor_num; a++) {
        // 当前 anchor 的通道起始索引
        int anchor_base_c = a * prop_per_anchor;
        
        for (int i = 0; i < grid_h; i++) {
            for (int j = 0; j < grid_w; j++) {
                // 当前网格点在 HW 平面的索引
                int hw_idx = i * grid_w + j;
                
                // 读取 objectness (从第5个通道开始)
                int obj_c = anchor_base_c + 4; // 前5个通道(值): tx ty tw th objectness
                int obj_idx = obj_c * grid_len + hw_idx;
                T box_confidence_quantized = input[obj_idx];
                
                // 快速过滤
                if (std::is_same<T, int8_t>::value && box_confidence_quantized < thres_quantized) {
                    continue;
                }
                
                float obj_conf = dequant(box_confidence_quantized);
                if (obj_conf < conf_thresh) continue;
                
                // 读取边界框参数 (前4个通道: tx, ty, tw, th)
                float tx = dequant(input[(anchor_base_c + 0) * grid_len + hw_idx]);
                float ty = dequant(input[(anchor_base_c + 1) * grid_len + hw_idx]);
                float tw = dequant(input[(anchor_base_c + 2) * grid_len + hw_idx]);
                float th = dequant(input[(anchor_base_c + 3) * grid_len + hw_idx]);

                // YOLOv5 解码公式
                float box_x = (tx * 2.0f - 0.5f + j) * static_cast<float>(stride);
                float box_y = (ty * 2.0f - 0.5f + i) * static_cast<float>(stride);
                float box_w = (tw * 2.0f) * (tw * 2.0f) * static_cast<float>(anchors[a].w);
                float box_h = (th * 2.0f) * (th * 2.0f) * static_cast<float>(anchors[a].h);

                // 找最大类别概率 (从第5个通道开始, 共 num_classes 个)
                int cls_base_c = anchor_base_c + 5;
                T max_class_prob_quantized = input[cls_base_c * grid_len + hw_idx];
                int max_class_id = 0;
                
                for (int k = 1; k < num_classes; ++k) {
                    int cls_c = cls_base_c + k;
                    int cls_idx = cls_c * grid_len + hw_idx;
                    T prob = input[cls_idx];
                    if (prob > max_class_prob_quantized) {
                        max_class_prob_quantized = prob;
                        max_class_id = k;
                    }
                }
                
                float max_cls_prob = dequant(max_class_prob_quantized);
                float final_conf = obj_conf * max_cls_prob;
                
                if (final_conf < conf_thresh) continue;
                
                // 输出
                rect_pos box;
                box.x = box_x - box_w / 2.0f;
                box.y = box_y - box_h / 2.0f;
                box.w = box_w;
                box.h = box_h;

                out_boxes.push_back(box);
                out_scores.push_back(final_conf);
                out_class_ids.push_back(max_class_id);
            }
        }
    }
}

// ------------------------------------------------------------
// 主后处理函数
// ------------------------------------------------------------
int post_process_rule(
    rknn_app_context& app_ctx,              // rknn上下文
    rknn_tensor_mem* out_mem[],             // 输出mem
    letterbox& lb,                          // 填充box
    std::vector<std::string>& class_names,  // label信息
    object_detect_result_list& results,     // 返回结果
    float conf_thresh,                      // 置信度阈值 
    float iou_thresh,                       // NMS阈值
    AnchorSet anchors                       // 可选anchors结构和数量
) {
    if (!out_mem) return -1;
    if (class_names.empty()) {
        fprintf(stderr, "Run [ postprocess::read_class_names ] first.\nOr check you coco.txt\n");
        return -1;
    }

    std::vector<rect_pos> all_boxes;    // 所有还原到原大小的box
    std::vector<float> all_scores;      // box置信度
    std::vector<int> all_class_ids;     // class id (0,1...,80...)
    int anchor_num = anchors.size();    // 通常为3
    results.clear();

    // 遍历所有输出 (yolov5: 3 或 1)
    for (int i = 0; i < app_ctx.io_num.n_output; ++i) {
        rknn_tensor_attr& attr = app_ctx.output_attrs[i];   // 取出输出信息

        int grid_h = attr.dims[2];  // 当前anchor层的网格高
        int grid_w = attr.dims[3];  // 宽
        // 下采样倍数(输入分辨率 / 特征图分辨率)
        int stride = app_ctx.model_height / grid_h;        // 8 << i 常见为 8,16,32
        int num_classes = attr.dims[1] / 3 - 5;             // 类别总数

        // 检查是否量化
        if (true == app_ctx.is_quant) {
            process_layer_rule<int8_t>(
                (int8_t*)out_mem[i]->virt_addr,
                grid_h, grid_w,
                stride, num_classes,
                conf_thresh,
                attr.zp, attr.scale,
                anchors[i],
                all_boxes, all_scores, all_class_ids
            );
        } else {
            process_layer_rule<float>(
                (float*)out_mem[i]->virt_addr,
                grid_h, grid_w,
                stride, num_classes,
                conf_thresh,
                0, 1.0f,
                anchors[i],
                all_boxes, all_scores, all_class_ids
            );
        }
    }

    std::vector<int> keep;  // 最后保留的 index
    nms_fast(all_boxes, all_scores, iou_thresh, keep);      // 快速NMS

    // 将box和对应的class name返回
    for (int idx : keep) {
        object_detect_result r;
        r.box.x = (all_boxes[idx].x - lb.x_pad) / lb.scale;
        r.box.y = (all_boxes[idx].y - lb.y_pad) / lb.scale;
        r.box.w = all_boxes[idx].w / lb.scale;
        r.box.h = all_boxes[idx].h / lb.scale;
        r.prop = all_scores[idx];
        r.class_id = all_class_ids[idx];
        r.class_name = (r.class_id >= 0 && r.class_id < (int)class_names.size()) ?
                       class_names[r.class_id] : "unknown";
        results.push_back(r);
    }

    // fprintf(stdout, "[postprocess] final %zu boxes\n", results.size());
    return 0;
}

} // namespace postprocess
