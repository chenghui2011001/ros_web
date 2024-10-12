#ifndef WEBSOCKETSERVER_H
#define WEBSOCKETSERVER_H
#include <ros/ros.h>
#include <signal.h>
#include <sensor_msgs/Image.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/time_synchronizer.h>

#include <sensor_msgs/image_encodings.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp> 
 
#include <functional>
 
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
 
#include <geometry_msgs/Twist.h>
#include <iostream>
#include <chrono>// 头文件用于时间相关操作
#include <thread>
#include <unordered_set>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
 
#include <boost/thread.hpp>
#include <std_msgs/String.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json; 
using namespace cv; 
using namespace std;
 
namespace std {
    template<>
    struct hash<websocketpp::connection_hdl> {
        std::size_t operator()(const websocketpp::connection_hdl& hdl) const {
            return std::hash<void*>{}(hdl.lock().get());
        }
    };
} 
cv::Mat createCustomStructuringElement(int width, int height) {
    if (width <= 0 || height <= 0) {
        std::cerr << "Error: Width and height must be positive." << std::endl;
        return cv::Mat();
    }

    cv::Mat element = cv::Mat::zeros(height, width, CV_8U);
    element.at<uchar>(height / 2, width / 2) = 1; // 中间点为1
    return element;
}

class WebSocketServer {
 
 
private:
    ros::NodeHandle node_handle_;
    ros::Publisher cmd_vel_publisher; 
    typedef websocketpp::server<websocketpp::config::asio> server;
    typedef server::message_ptr MessagePtr;
    server ws_server;
    ros::Subscriber string_subscriber_;
    ros::Subscriber cmd_vel_subscriber_;  // 订阅 cmd_vel 的成员变量
    cv::Mat rgb_image;  // 在类中定义 rgb_image
    cv::Mat hsv_image;  // 在类中定义 hsv_image
    
    std::unordered_set<websocketpp::connection_hdl, std::hash<websocketpp::connection_hdl>, std::owner_less<websocketpp::connection_hdl>> connected_clients;
    std::mutex client_mutex;
    std::atomic<bool> isLocked;  // 声明成员变量
    //std::atomic<bool> isLocked(false);  // 全局标志变量
    ros::Timer   timer_;
    int imageNum = 0;
    cv::CascadeClassifier cascade;
    
    double pid_error = 0.0;    // 当前误差
    double pid_integral = 0.0;  // 积分值
    double pid_derivative = 0.0; // 微分值
    double last_error = 0.0;     // 上一个误差
    // ROS 控制变量
    bool is_tracking = false;
    bool is_manual = false;
 
//编码base64数据
    static std::string base64Encode(const unsigned char *Data, int DataByte) {
        //编码表
        const char EncodeTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        //返回值
        std::string strEncode;
        unsigned char Tmp[4] = {0};
        int LineLength = 0;
        for (int i = 0; i < (int) (DataByte / 3); i++) {
            Tmp[1] = *Data++;
            Tmp[2] = *Data++;
            Tmp[3] = *Data++;
            strEncode += EncodeTable[Tmp[1] >> 2];
            strEncode += EncodeTable[((Tmp[1] << 4) | (Tmp[2] >> 4)) & 0x3F];
            strEncode += EncodeTable[((Tmp[2] << 2) | (Tmp[3] >> 6)) & 0x3F];
            strEncode += EncodeTable[Tmp[3] & 0x3F];
            if (LineLength += 4, LineLength == 76) {
                strEncode += "\r\n";
                LineLength = 0;
            }
        }
        //对剩余数据进行编码
        int Mod = DataByte % 3;
        if (Mod == 1) {
            Tmp[1] = *Data++;
            strEncode += EncodeTable[(Tmp[1] & 0xFC) >> 2];
            strEncode += EncodeTable[((Tmp[1] & 0x03) << 4)];
            strEncode += "==";
        } else if (Mod == 2) {
            Tmp[1] = *Data++;
            Tmp[2] = *Data++;
            strEncode += EncodeTable[(Tmp[1] & 0xFC) >> 2];
            strEncode += EncodeTable[((Tmp[1] & 0x03) << 4) | ((Tmp[2] & 0xF0) >> 4)];
            strEncode += EncodeTable[((Tmp[2] & 0x0F) << 2)];
            strEncode += "=";
        }
 
 
        return strEncode;
    }
 
 
    //imgType 包括png bmp jpg jpeg等opencv可以进行编码解码的文件
    static std::string Mat2Base64(const cv::Mat &img, std::string imgType) {
        //Mat转base64
        std::string img_data;
        std::vector<uchar> vecImg;
        std::vector<int> vecCompression_params;
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
 
        vecCompression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
        vecCompression_params.push_back(40);// 根据需要调整此值
        imgType = "." + imgType;
        //重点来了，它是负责把图像从opencv的Mat变成编码好的图像比特流的重要函数
        cv::imencode(imgType, img, vecImg, vecCompression_params);
        img_data = base64Encode(vecImg.data(), vecImg.size());
        return img_data;
    }
 
 
 
public:
    // PID 参数
    static double pid_p;
    static double pid_i;
    static double pid_d;
    static int iLowH;
    static int iHighH;
    static int iLowS;
    static int iHighS;
    static int iLowV;
    static int iHighV; 
    WebSocketServer(ros::NodeHandle& nh) :node_handle_("~"), isLocked(true)  {
 
        initvariable(node_handle_);
 
 
        cmd_vel_publisher = node_handle_.advertise<geometry_msgs::Twist>("/cmd_vel", 10);
        ws_server.set_message_handler(std::bind(&WebSocketServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
        ws_server.set_open_handler(std::bind(&WebSocketServer::on_open,this, ::websocketpp::lib::placeholders::_1));
        ws_server.set_close_handler(std::bind(&WebSocketServer::on_close, this, websocketpp::lib::placeholders::_1));
        ws_server.set_message_handler(std::bind(&WebSocketServer::on_message, this, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));

        // 新增 cmd_vel 订阅配置
        cmd_vel_subscriber_ = nh.subscribe("/cmd_vel", 1, &WebSocketServer::cmdVelCallback, this);
        // 设置消息处理函数   websocketpp::config::asio::message_type::ptr msg
        ws_server.set_message_handler([this](websocketpp::connection_hdl hdl, server::message_ptr msg) {
            on_message(hdl, msg);
        });
 
 
        // 设置失败处理函数
        ws_server.set_fail_handler([this](websocketpp::connection_hdl hdl) {
            on_fail(hdl);
        });
 
        // 设置关闭处理函数
        ws_server.set_close_handler([this](websocketpp::connection_hdl hdl) {
            this->on_close(hdl);
        });
 
        // 设置连接打开处理函数
        ws_server.set_open_handler([this](websocketpp::connection_hdl hdl) {
            on_open(hdl);
        });
 
        //set logging settings
        ws_server.clear_access_channels(websocketpp::log::alevel::all);
 
        ws_server.init_asio();
        ws_server.set_reuse_addr(true);
        ws_server.listen(websocketpp::lib::asio::ip::tcp::v4(),9030);
        ws_server.start_accept();
 
    }
 
    ~WebSocketServer() {
        ws_server.stop_listening();
        ws_server.stop();
        ros::shutdown();
        if (ros::isShuttingDown()){
            cout<<"close ros  test.cpp"<<endl;
        }
        // server_thread.join();
    }
 
    void thread_msg() {
 
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ros::spinOnce();
        }
    }
 
 
    // 处理 /cmd_vel 消息的回调函数
    void cmdVelCallback(const geometry_msgs::Twist::ConstPtr& msg) {
        // 提取 linear.x 和 angular.z 的值
        float linear_x = msg->linear.x;
        float angular_z = msg->angular.z;

        // 创建 JSON 数据包
        json cmd_vel_data;
        cmd_vel_data["label"]="cmd_vel";
        cmd_vel_data["linear_x"] = linear_x;
        cmd_vel_data["angular_z"] = angular_z;

        // 发送给所有连接的 WebSocket 客户端
        std::lock_guard<std::mutex> lock(client_mutex);
        for (auto hdl : connected_clients) {
            try {
                ws_server.send(hdl, cmd_vel_data.dump(), websocketpp::frame::opcode::text);
            } catch (const websocketpp::lib::error_code& e) {
                ROS_ERROR("Error sending cmd_vel data to client: %s", e.message().c_str());
            }
        }
    }

    void multi_callback(const sensor_msgs::ImageConstPtr& rgb, const sensor_msgs::ImageConstPtr& depth) {
        if (!rgb || !depth) {
            ROS_ERROR("Received null image data.");
            return;
        }

        try {
            // 获取 RGB 图像数据
            rgb_image = cv_bridge::toCvCopy(rgb, sensor_msgs::image_encodings::BGR8)->image;

            if (rgb_image.empty()) {
                ROS_ERROR("Converted RGB image is empty.");
                return;
            }

            cv::Mat temp_hsv_image;

            // 转换 RGB 到 HSV
            cv::cvtColor(rgb_image, temp_hsv_image, cv::COLOR_BGR2HSV);

            // 确保 temp_hsv_image 有效
            if (temp_hsv_image.empty()) {
                ROS_ERROR("HSV image conversion failed.");
                return;
            }

            // HSV 处理
            vector<cv::Mat> hsvSplit;
            split(temp_hsv_image, hsvSplit);
            equalizeHist(hsvSplit[2], hsvSplit[2]);
            merge(hsvSplit, temp_hsv_image); 

            // 阈值分割
            cv::Mat imgThresholded;
            
            inRange(temp_hsv_image, Scalar(iLowH, iLowS, iLowV), Scalar(iHighH, iHighS, iHighV), imgThresholded);
            
            if (is_tracking) { // 检查是否启用循迹
                cv::Moments M = cv::moments(imgThresholded, true);
                if (M.m00 > 0) {
                    int cx = static_cast<int>(M.m10 / M.m00);
                    int width = imgThresholded.cols;
                    
                    pid_error = cx - (width / 2.0);
                    pid_integral += pid_error;
                    pid_derivative = pid_error - last_error;
                    double control_signal = (pid_p * pid_error) + (pid_i * pid_integral) + (pid_d * pid_derivative);
                    ROS_INFO("pid_p: %.2f, pid_i: %.2f, pid_d: %.2f", pid_p, pid_i,pid_d);
                    last_error = pid_error;

                    geometry_msgs::Twist twist;
                    twist.linear.x = 0.1; 
                    twist.angular.z = -control_signal/500 ;

                    cmd_vel_publisher.publish(twist);
                    ROS_INFO("Twist: linear.x: %.2f, angular.z: %.2f", twist.linear.x, twist.angular.z);
                    // 创建 JSON 数据包发送误差值
                    json errorData;
                    errorData["label"] = "PID_Error";
                    errorData["error"] = pid_error;

                    // 发送误差值给所有连接的 WebSocket 客户端
                    std::lock_guard<std::mutex> lock(client_mutex);
                    for (auto hdl : connected_clients) {
                        try {
                            ws_server.send(hdl, errorData.dump(), websocketpp::frame::opcode::text);
                        } catch (const websocketpp::lib::error_code& e) {
                            ROS_ERROR("Error sending PID error data to client: %s", e.message().c_str());
                        }
                    }
                } else {
                    geometry_msgs::Twist stop_twist;
                    stop_twist.linear.x = 0.0;
                    stop_twist.angular.z = 0.0;
                    cmd_vel_publisher.publish(stop_twist);
                    ROS_WARN("No target found, stopping.");
                }
            }else if (!is_manual){
                    geometry_msgs::Twist stop_twist;
                    stop_twist.linear.x = 0.0;
                    stop_twist.angular.z = 0.0;
                    cmd_vel_publisher.publish(stop_twist);
            }
            
        
            std::vector<uchar> rgb_buffer, hsv_buffer, result_buffer;
            if (!cv::imencode(".jpg", rgb_image, rgb_buffer)) {
                ROS_ERROR("Failed to encode RGB image.");
                return;
            }
            
            if (!cv::imencode(".jpg", temp_hsv_image, hsv_buffer)) {
                ROS_ERROR("Failed to encode HSV image.");
                return;
            }

            if (!cv::imencode(".jpg", imgThresholded, result_buffer)) {
                ROS_ERROR("Failed to encode Result image.");
                return;
            }

            std::lock_guard<std::mutex> lock(client_mutex);
            for (auto hdl : connected_clients) {
                try {
                    // Send encoded images as JSON
                    json rgbData;
                    rgbData["label"] = "RGB";
                    rgbData["image"] = base64Encode(rgb_buffer.data(), rgb_buffer.size());
                    ws_server.send(hdl, rgbData.dump(), websocketpp::frame::opcode::text);
                    
                    json hsvData;
                    hsvData["label"] = "HSV";
                    hsvData["image"] = base64Encode(hsv_buffer.data(), hsv_buffer.size());
                    ws_server.send(hdl, hsvData.dump(), websocketpp::frame::opcode::text);
                    
                    json resultData;
                    resultData["label"] = "Result";
                    resultData["image"] = base64Encode(result_buffer.data(), result_buffer.size());
                    ws_server.send(hdl, resultData.dump(), websocketpp::frame::opcode::text);
                } catch (const websocketpp::lib::error_code& e) {
                    ROS_ERROR("Error sending image to client: %s", e.message().c_str());
                }
            }
        } catch (const cv::Exception& e) {
            ROS_ERROR("OpenCV error: %s", e.what());
        } catch (const std::exception& e) {
            ROS_ERROR("Error in multi_callback: %s", e.what());
        }
    }



 
    void stop() {
        // if (ws_server.is_running()) {
        ws_server.stop_listening();
        ws_server.stop();
        //server_thread.join();
        // }
 
 
    }
    void run() {
        ws_server.run();
 
    }
 
 
 
    ros::NodeHandle nh_;  // ROS节点句柄
    message_filters::Subscriber<sensor_msgs::Image> sub1_;  // 图像消息订阅者1
    message_filters::Subscriber<sensor_msgs::Image> sub2_;  // 图像消息订阅者2
    typedef message_filters::TimeSynchronizer<sensor_msgs::Image, sensor_msgs::Image> Sync;
    boost::shared_ptr<Sync> sync_;  // 时间同步器
 
    void stringCallback(const std_msgs::String::ConstPtr& msg) {
        // 处理接收到的 std_msgs::String 消息的逻辑
        std::cout << "处理接收到的 std_msgs::String 消息的逻辑: " << msg->data << std::endl;
    }
 
 
    void initvariable(ros::NodeHandle& nh) {
        string_subscriber_ = nh.subscribe("/your/string_topic", 1, &WebSocketServer::stringCallback, this);
        // 初始化两个图像消息的订阅者
        sub1_.subscribe(nh_, "/kinect2/qhd/image_color_rect", 1);
        sub2_.subscribe(nh_, "/kinect2/sd/image_depth_rect", 1);
 
        // 设置时间同步器，设置时间同步容忍度，这里设置为0.1秒
        sync_.reset(new message_filters::TimeSynchronizer<sensor_msgs::Image, sensor_msgs::Image>(sub1_, sub2_, 10));
        sync_->registerCallback(boost::bind(&WebSocketServer::multi_callback, this, _1, _2));
 
        boost::thread parse_thread(boost::bind(&WebSocketServer::thread_msg, this));
 
    }
 
    void on_fail( websocketpp::connection_hdl hdl) {
 
        try {
 
            std::cout << "on_fail "  << std::endl;
 
        } catch (const std::exception& e) {
            std::cerr << "Error on_fail: " << e.what() << std::endl;
 
        }
 
    }
 
    void on_open(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(client_mutex);
        connected_clients.insert(hdl);
    }

    void on_close(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(client_mutex);
        connected_clients.erase(hdl);
    }
 
 
 // 其他类定义和成员函数...

    void on_message(websocketpp::connection_hdl hdl, const server::message_ptr& msg) {
        try {
            if (msg->get_payload().empty()) {
                ROS_WARN("Received empty message from client");
                return;
            }

            json payload = json::parse(msg->get_payload());
            if (payload.contains("action")) {
                if (payload["action"] == "start_tracking") {
                    is_tracking = true;
                    ROS_INFO("Starting tracking.");
                } else if (payload["action"] == "stop_tracking") {
                    is_tracking = false;
                    ROS_INFO("Stopping tracking.");
                }
            }
            if (payload.contains("P")){ {
                    pid_p = payload["P"];
                    pid_i = payload["I"];
                    pid_d = payload["D"];
                }
                ROS_INFO("Updated PID parameters: P=%.2f, I=%.2f, D=%.2f", pid_p, pid_i, pid_d);
            }
            // 处理 cmd_vel_data
            if (payload.contains("cmd_vel_data")) {
                auto cmd_vel_data = payload["cmd_vel_data"];
                if (cmd_vel_data.contains("linear_x") && cmd_vel_data.contains("angular_z")) {
                    float linear_x = cmd_vel_data["linear_x"];
                    float angular_z = cmd_vel_data["angular_z"];
                    is_manual = true; 
                    // 创建一个 Twist 消息
                    geometry_msgs::Twist cmd_vel_msg;
                    cmd_vel_msg.linear.x = linear_x;
                    cmd_vel_msg.angular.z = angular_z;

                    // 发布消息到 /cmd_vel 话题
                    cmd_vel_publisher.publish(cmd_vel_msg); // 使用正确初始化的发布器
                    
                } else {
                    ROS_WARN("Missing linear_x or angular_z in cmd_vel_data.");
                }
            } 
            else if (payload.contains("iLowH")) {
                // 更新 HSV 参数
                iLowH = payload["iLowH"];
                iHighH = payload["iHighH"];
                iLowS = payload["iLowS"];
                iHighS = payload["iHighS"];
                iLowV = payload["iLowV"];
                iHighV = payload["iHighV"];
                
            } else {
                // 检查图像数据大小
                size_t rgbDataSize = rgb_image.total() * rgb_image.elemSize();
                size_t hsvDataSize = hsv_image.total() * hsv_image.elemSize();

                if (msg->get_payload().size() == rgbDataSize) {
                    ROS_INFO("Received RGB image from client");
                    // 处理 RGB 图像数据（
                } else if (msg->get_payload().size() == hsvDataSize) {
                    ROS_INFO("Received HSV image from client");
                    // 处理 HSV 图像数据
                } else {
                    ROS_WARN("Received unexpected data size from client: %zu", msg->get_payload().size());
                }
            }
        } catch (const json::parse_error& e) {
            ROS_ERROR("JSON parse error: %s", e.what());
        } catch (const std::exception& e) {
            ROS_ERROR("Error processing message from client: %s", e.what());
        }
    }


};
#endif // WEBSOCKETSERVER_H

