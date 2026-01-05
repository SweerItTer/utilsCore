#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <sstream>

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);

    bool good() const;

    void writeHeader(const std::vector<std::string>& columns);

    template <typename T>
    void writeRow(const std::vector<T>& values);

private:
    std::ofstream ofs;

    template <typename T>
    void writeValue(const T& v);

    void writeSeparator();
};

CsvWriter::CsvWriter(const std::string& path)
    : ofs(path) {
}

bool CsvWriter::good() const {
    return ofs.good();
}

void CsvWriter::writeHeader(
    const std::vector<std::string>& columns) {

    for (size_t i = 0; i < columns.size(); ++i) {
        ofs << columns[i];
        if (i + 1 < columns.size()) {
            ofs << ",";
        }
    }
    ofs << "\n";
}

void CsvWriter::writeSeparator() {
    ofs << ",";
}

template <typename T>
void CsvWriter::writeValue(const T& v) {
    ofs << v;
}

/*
 * 注意:
 * 由于这是模板函数, 实现必须放在头文件中
 */
template <typename T>
void CsvWriter::writeRow(const std::vector<T>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        writeValue(values[i]);
        if (i + 1 < values.size()) {
            writeSeparator();
        }
    }
    ofs << "\n";
}

/* 显式实例化常用类型, 防止链接问题 */
template void CsvWriter::writeRow<int>(const std::vector<int>&);
template void CsvWriter::writeRow<unsigned int>(const std::vector<unsigned int>&);
template void CsvWriter::writeRow<double>(const std::vector<double>&);
template void CsvWriter::writeRow<std::string>(const std::vector<std::string>&);
