#include <ros/ros.h>
#include <signal.h>
#include <sensor_msgs/Image.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/time_synchronizer.h>
 
 
#include <functional>
 
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
 
 
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
 
using namespace std;
 
namespace std {
    template<>
    struct hash<websocketpp::connection_hdl> {
        std::size_t operator()(const websocketpp::connection_hdl& hdl) const {
            return std::hash<void*>{}(hdl.lock().get());
        }
    };
} 
 
class WebSocketServer {
 
 
private:
    ros::NodeHandle node_handle_;
    typedef websocketpp::server<websocketpp::config::asio> server;
    typedef server::message_ptr MessagePtr;
    server ws_server;
    ros::Subscriber string_subscriber_;
    cv::Mat rgb_image;  // 在类中定义 rgb_image
    cv::Mat hsv_image;  // 在类中定义 hsv_image
    // 同步客户端连接的结构
    // std::unordered_set<std::shared_ptr<websocketpp::connection_hdl>> connected_clients;
    // 更改成为 std::shared_ptr，便于管理连接状态
    //std::unordered_set<std::shared_ptr<websocketpp::connection_hdl>> disconnected_clients;
    std::unordered_set<websocketpp::connection_hdl, std::hash<websocketpp::connection_hdl>, std::owner_less<websocketpp::connection_hdl>> connected_clients;
    std::mutex client_mutex;
    std::atomic<bool> isLocked;  // 声明成员变量
    //std::atomic<bool> isLocked(false);  // 全局标志变量
    ros::Timer   timer_;
    int imageNum = 0;
    cv::CascadeClassifier cascade;
 
 
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
 
    WebSocketServer(ros::NodeHandle& nh) :node_handle_("~"), isLocked(true)  {
 
        initvariable(node_handle_);
 
 
 
        ws_server.set_message_handler(std::bind(&WebSocketServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
        ws_server.set_open_handler(std::bind(&WebSocketServer::on_open,this, ::websocketpp::lib::placeholders::_1));
        ws_server.set_close_handler(std::bind(&WebSocketServer::on_close, this, websocketpp::lib::placeholders::_1));
        ws_server.set_message_handler(std::bind(&WebSocketServer::on_message, this, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
 
 
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
 
 
 

    void multi_callback(const sensor_msgs::ImageConstPtr& rgb, const sensor_msgs::ImageConstPtr& depth) {
        // 获取 RGB 图像数据
        rgb_image = cv_bridge::toCvCopy(rgb, sensor_msgs::image_encodings::BGR8)->image;
        // 获取 HSV 图像数据
        cv::Mat temp_hsv_image;
        cv::cvtColor(rgb_image, temp_hsv_image, cv::COLOR_BGR2HSV);

        std::lock_guard<std::mutex> lock(client_mutex);
        for (auto it = connected_clients.begin(); it != connected_clients.end();) {
            if (it->use_count() == 0) {
                it = connected_clients.erase(it);
            } else {
                ++it;
            }
        }

        try {
        // 编码 RGB 和 HSV 图像数据
        std::vector<uchar> rgb_buffer, hsv_buffer;
        cv::imencode(".jpg", rgb_image, rgb_buffer);
        cv::imencode(".jpg", temp_hsv_image, hsv_buffer);

        for (auto hdl : connected_clients) {
            try {
                // 创建 JSON 对象
                json rgbData;
                rgbData["label"] = "RGB";
                rgbData["image"] = base64Encode(rgb_buffer.data(), rgb_buffer.size());

                // 发送带标签的 RGB 图像数据
                ws_server.send(hdl, rgbData.dump(), websocketpp::frame::opcode::text);
                ROS_INFO("Sent RGB image to client");

                // 创建 JSON 对象
                json hsvData;
                hsvData["label"] = "HSV";
                hsvData["image"] = base64Encode(hsv_buffer.data(), hsv_buffer.size());

                // 发送带标签的 HSV 图像数据
                ws_server.send(hdl, hsvData.dump(), websocketpp::frame::opcode::text);
                ROS_INFO("Sent HSV image to client");
            } catch (const websocketpp::lib::error_code& e) {
                ROS_ERROR("Error sending image to client: %s", e.message().c_str());
            }
        }
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

        size_t rgbDataSize = rgb_image.total() * rgb_image.elemSize();
        size_t hsvDataSize = hsv_image.total() * hsv_image.elemSize();

        if (msg->get_payload().size() == rgbDataSize) {
            ROS_INFO("Received RGB image from client");
            // 处理接收到的 RGB 图像数据
            // 例如：将 msg->get_payload() 转换为 OpenCV 图像格式并进行处理
        } else if (msg->get_payload().size() == hsvDataSize) {
            ROS_INFO("Received HSV image from client");
            // 处理接收到的 HSV 图像数据
            // 例如：将 msg->get_payload() 转换为 OpenCV 图像格式并进行处理
        } else {
            ROS_WARN("Received unexpected image data from client");
        }
    } catch (const std::exception& e) {
            ROS_ERROR("Error processing message from client: %s", e.what());
        }
}
};

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
