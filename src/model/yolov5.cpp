/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-03 20:05:26
 * @FilePath: /EdgeVision/src/model/yolov5.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "yolov5.h"

static void dump_tensor_attr(const rknn_tensor_attr& attr) {
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], "
           "n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, zp=%d, scale=%f\n",
           attr.index, attr.name, attr.n_dims,
           attr.dims[0], attr.dims[1], attr.dims[2], attr.dims[3],
           attr.n_elems, attr.size,
           get_format_string(attr.fmt),
           get_type_string(attr.type),
           get_qnt_type_string(attr.qnt_type),
           attr.zp, attr.scale);
}

int loadModel(const char* model_path, rknn_app_context& app_ctx) {
    if (nullptr == model_path) {
        fprintf(stderr, "Invalid model path\n");
        return -1;
    }

    char* model = nullptr;
    // 获取模型数据
    int model_size = read_data_from_file(model_path, &model);
    if (nullptr == model || model_size <= 0) {
        std::cerr << "Read model failed\n";
        return -1;
    }

    // 初始化rknn上下文,传入模型内容和大小
    rknn_context& ctx = app_ctx.rknn_ctx;
    int ret = rknn_init(&ctx, model, model_size, 0, nullptr);
    free(model);

    if (ret < 0) {
        fprintf(stderr, "rknn_init fail! ret=%d\n", ret);
        return -1;
    }

    return 0;
}

int loadIOnum(rknn_app_context &app_ctx) {
    rknn_context& ctx = app_ctx.rknn_ctx;
    rknn_input_output_num& io_num = app_ctx.io_num;

    // 获取模型 IN_OUT 信息
    int ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        fprintf(stderr, "rknn_query IO num fail! ret=%d\n", ret);
        return -1;
    }

    fprintf(stdout, "model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    // 获取 输入张量
    app_ctx.input_attrs.resize(io_num.n_input);
    // printf("input tensors:\n"); // 初始化 INPUT tensor属性
    for (int i = 0; i < io_num.n_input; i++) {
        app_ctx.input_attrs[i].index = i;   // 设置索引
        // 查询 INPUT tensor属性(rknn_query填充到input_attrs[i])
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(app_ctx.input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            fprintf(stderr, "rknn_query fail! ret=%d\n", ret);
            return -1;
        }
        // 输出tensor属性
        // dump_tensor_attr(app_ctx.input_attrs[i]);
    }

    // Get Model Output Info
    app_ctx.output_attrs.resize(io_num.n_output);
    // printf("output tensors:\n");
    for (int i = 0; i < io_num.n_output; ++i) {
        app_ctx.output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &app_ctx.output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            fprintf(stderr, "rknn_query output_attr fail! ret=%d\n", ret);
            return -1;
        }
        // dump_tensor_attr(app_ctx.output_attrs[i]);
    }

    if (app_ctx.output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC 
        && app_ctx.output_attrs[0].type != RKNN_TENSOR_FLOAT16) {
        // 输出定量类型: 非对称仿射 且 输出数据类型: 非FP16
        app_ctx.is_quant = true;
    } else {
        app_ctx.is_quant = false;
    }

    // 根据 INPUT tensor属性, 保存模型输入尺寸
    if (app_ctx.input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        fprintf(stdout, "model is NCHW input fmt\n");
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[3];
    }
    else
    {
        fprintf(stdout, "model is NHWC input fmt\n");
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[3];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[2];
    }
    // 输出模型输入尺寸
    // printf("model input height=%d, width=%d, channel=%d\n",
    //        app_ctx.model_height, app_ctx.model_width, app_ctx.model_channel);
    return ret;
}

int initializeMems(rknn_app_context &app_context) {
    int ret = 0;
    
    auto& slot = app_context.io_mem;
    app_context.input_attrs[0].type = RKNN_TENSOR_UINT8;
    app_context.input_attrs[0].fmt = RKNN_TENSOR_NHWC;
    // RKNN 内部创建 mem, 预处理通过 input_mems.fd 交由 RGA 处理
    slot.input_mems[0] = rknn_create_mem(app_context.rknn_ctx, app_context.input_attrs[0].size_with_stride);
    // 测试是否可用(后续根据具体情况获取可用input_mems来使用)
    ret = rknn_set_io_mem(app_context.rknn_ctx, slot.input_mems[0], &app_context.input_attrs[0]);
    if (ret < 0) {
        fprintf(stderr, "input_mems rknn_set_io_mem fail! ret=%d\n", ret);
        return -1;
    }
    for (uint32_t i = 0; i < app_context.io_num.n_output; ++i) {
        slot.output_mems[i] = rknn_create_mem(app_context.rknn_ctx, app_context.output_attrs[i].size_with_stride);
        ret = rknn_set_io_mem(app_context.rknn_ctx, slot.output_mems[i], &app_context.output_attrs[i]);
        if (ret < 0) {
            fprintf(stderr, "output_mems rknn_set_io_mem fail! ret=%d\n", ret);
            return -1;
        }
    }
    return ret;
}

rknn_app_context::~rknn_app_context() {
    
    for (int i = 0; i < io_num.n_input; i++) {
        if (io_mem.input_mems[i]) {
            rknn_destroy_mem(rknn_ctx, io_mem.input_mems[i]);
            io_mem.input_mems[i] = nullptr;
        }
    }
    for (int i = 0; i < io_num.n_output; i++) {
        if (io_mem.output_mems[i]) {
            rknn_destroy_mem(rknn_ctx, io_mem.output_mems[i]);
            io_mem.output_mems[i] = nullptr;
        }
    }

    if (rknn_ctx) {
        rknn_destroy(rknn_ctx);
        rknn_ctx = 0;
    }

    input_attrs.clear();
    output_attrs.clear();
    
    fprintf(stdout, "[Cleanup] Releasing resources...\n");
}

rknn_app_context::rknn_app_context(rknn_app_context&& other) noexcept {
    *this = std::move(other);
}

rknn_app_context& rknn_app_context::operator=(rknn_app_context&& other) noexcept {
    if (this != &other) {
        std::swap(rknn_ctx, other.rknn_ctx);
        std::swap(io_num, other.io_num);
        std::swap(input_attrs, other.input_attrs);
        std::swap(output_attrs, other.output_attrs);
        std::swap(model_channel, other.model_channel);
        std::swap(model_width, other.model_width);
        std::swap(model_height, other.model_height);
        std::swap(is_quant, other.is_quant);
        std::swap(io_mem, other.io_mem);

        other.rknn_ctx = 0;
        other.io_num = {};
        other.input_attrs.clear();
        other.output_attrs.clear();
        other.model_channel = 0;
        other.model_width = 0;
        other.model_height = 0;
        other.is_quant = false;
        other.io_mem = {};
    }
    return *this;
}