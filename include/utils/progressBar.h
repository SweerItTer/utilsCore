/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-19 21:37:59
 * @FilePath: /include/utils/progressBar.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <iostream>
#include <string>
#include <mutex>
#include <vector>
#include <iomanip>

class ProgressManager {
public:
    struct Bar {
        std::string name;
        int value = 0;         // 当前进度 (0-100)
        int total = 100;       // 总进度 (默认100)
        int width = 50;        // 显示宽度
    };

private:
    std::vector<Bar> bars;
    std::mutex mtx;

public:
    int addBar(const std::string& name, int total = 100) {
        std::lock_guard<std::mutex> lock(mtx);
        bars.push_back({name, 0, total});
        return bars.size() - 1; // 返回索引
    }

    void update(int index, int current) {
        std::lock_guard<std::mutex> lock(mtx);
        if (index < 0 || index >= (int)bars.size()) return;
        bars[index].value = current;
        draw();
    }

    void done(int index) {
        std::lock_guard<std::mutex> lock(mtx);
        if (index < 0 || index >= (int)bars.size()) return;
        bars[index].value = bars[index].total;
        draw();
    }

    void draw() {
        std::cout << "\033[" << bars.size() << "A"; // 光标上移到顶部
        for (const auto& bar : bars) {
            int percent = (100 * bar.value) / bar.total;
            int filled = (bar.width * bar.value) / bar.total;
            std::cout << "\r" << bar.name << " ["
                      << std::string(filled, '=') 
                      << std::string(bar.width - filled, ' ')
                      << "] "
                      << std::setw(3) << percent << "%   \n";
        }
        std::cout.flush();
    }
    
};
