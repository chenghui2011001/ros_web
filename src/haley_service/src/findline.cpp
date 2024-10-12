#include <ros/ros.h>
#include <signal.h>
#include <sensor_msgs/Image.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/time_synchronizer.h>
#include <functional>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
#include <boost/thread.hpp>
#include <std_msgs/String.h>
#include <nlohmann/json.hpp>
#include <WebSocketServer.h>

using json = nlohmann::json;

using namespace std;
using namespace cv;


// PID 参数初始化
double WebSocketServer::pid_p = 0.0;
double WebSocketServer::pid_i = 0.0;
double WebSocketServer::pid_d = 0.0;
int WebSocketServer::iLowH = 10;
int WebSocketServer::iHighH = 40;
int WebSocketServer::iLowS = 90;
int WebSocketServer::iHighS = 255;
int WebSocketServer::iLowV = 1;
int WebSocketServer::iHighV = 255;

WebSocketServer *webSocketServer = nullptr;

void mySigintHandler(int sig) {
    try {
        std::cout << "ctrl c  sig: " << sig << std::endl;
        if (webSocketServer != nullptr) {
            delete webSocketServer;
            webSocketServer = nullptr;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in mySigintHandler: " << e.what() << std::endl;
        if (webSocketServer != nullptr) {
            delete webSocketServer;
            webSocketServer = nullptr;
        }
    }
    exit(sig);
}

// 在 main 函数中，创建动态分配的 WebSocketServer 对象
int main(int argc, char** argv) {
    setlocale(LC_ALL, "");  // 设置本地化
    string nodeName = "hsv_node";
    ros::init(argc, argv, nodeName);
    ros::NodeHandle node;

    WebSocketServer* myWebSocketServer = nullptr;
    
    try {
        myWebSocketServer = new WebSocketServer(node);  // 实例化 WebSocketServer 类
        webSocketServer = myWebSocketServer;  // 将服务实例指针存储在全局变量中

        signal(SIGINT, mySigintHandler);

        // 运行 WebSocket 服务器
        myWebSocketServer->run();
    } catch (const std::exception& e) {
        ROS_ERROR("Error in WebSocketServer initialization: %s", e.what());
        if (myWebSocketServer != nullptr) {
            delete myWebSocketServer;  // 如果出现异常，确保释放已分配的资源
        }
    }

    std::cerr << "End main" << std::endl;
    return 0;
}
