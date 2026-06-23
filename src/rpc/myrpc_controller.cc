#include "myrpc_controller.h"

MyRpcController::MyRpcController() : failed_(false) {} // error_info_自动为空字符串

/// @brief 重置控制器状态
void MyRpcController::Reset() {
    failed_ = false; // 初始状态为“未失败”
    error_info_.clear();
}

bool MyRpcController::Failed() const {
    return failed_;
}

std::string MyRpcController::ErrorText() const {
    return error_info_;
}

/// @brief 开始取消RPC调用（未实现）
void MyRpcController::StartCancel() {} // 暂不实现

void MyRpcController::SetFailed(const std::string& reason) {
    failed_ = true;
    error_info_ = reason;
}
/// @brief 判断RPC调用是否被取消（未实现）
/// @return
bool MyRpcController::IsCanceled() const {
    return false; // 暂不实现
}
/// @brief 注册取消回调函数（未实现）
/// @param callback
void MyRpcController::NotifyOnCancel(Closure* callback) {} // 暂不实现
