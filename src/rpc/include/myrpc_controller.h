#pragma once

#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>

using google::protobuf::Closure;

class MyRpcController : public google::protobuf::RpcController {
  public:
    MyRpcController();
    // 实现RpcController中的虚函数
    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void StartCancel() override; // 暂未实现-目前为空实现
    void SetFailed(const std::string& reason) override;
    bool IsCanceled() const override;                // 暂未实现-目前为空实现
    void NotifyOnCancel(Closure* callback) override; // 暂未实现-目前为空实现

  private:
    bool failed_;            // rpc方法执行状态
    std::string error_info_; // rpc方法执行过程中的错误信息描述
};