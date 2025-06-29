#include <iostream>
#include <QApplication>

#ifdef RK3568
#include <string>
#include <cstdlib>

// 判断服务是否运行
bool isServiceRunning(const std::string& serviceName) {
    // 构造命令
    std::string command = "ps -e | grep " + serviceName;

    // 使用 popen 执行命令并获取输出
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to execute command." << std::endl;
        return false;
    }

    char buffer[128];
    std::string result = "";

    // 读取命令输出
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    // 关闭管道
    pclose(pipe);

    // 如果输出中包含服务名，说明服务正在运行
    return result.find(serviceName) != std::string::npos;
}

// 启动服务
void startService(const std::string& serviceName) {
    std::string command = serviceName + " &"; // 后台运行
    int result = std::system(command.c_str());

    if (result == 0) {
        std::cout << "Service started successfully." << std::endl;
    } else {
        std::cerr << "Error: Failed to start service." << std::endl;
    }
}
#endif // RK3568

int main(int argc, char const *argv[])
{
    #ifdef RK3568
	
	std::string serviceName = "ispserver"; // 开启服务

    // 检查服务是否运行
    if (!isServiceRunning(serviceName)) {
        std::cout << "Service is not running. Attempting to start..." << std::endl;
        startService(serviceName);
    } else {
        std::cout << "Service is already running." << std::endl;
    }
	
	#endif // RK3568
    QApplication a(argc, argv);
	MainInterface w;
	w.show();
	return a.exec();
}
