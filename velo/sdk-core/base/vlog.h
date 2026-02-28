#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace base {

enum LogLevel { INFO, WARN, ERR, FATAL };

class VLog {
public:
  VLog() = default;
  
  template <typename T> VLog &operator<<(const T &value) {
    buffer_ << value;
    return *this;
  }

  // 遇到日志等级时，视为输出终止符
  VLog &operator<<(LogLevel level) {
    std::string prefix;
    switch (level) {
    case INFO:
      prefix = "[INFO] ";
      break;
    case WARN:
      prefix = "[WARN] ";
      break;
    case ERR:
      prefix = "[ERR ] ";
      break;
    case FATAL:
      prefix = "[FATAL] ";
      break;
    }

    // 输出最终日志：前缀 + 缓冲区内容 + 换行
    std::cout << prefix << buffer_.str() << std::endl;

    // 清空缓冲区，为下一次输出做准备
    buffer_.str("");
    buffer_.clear();

    if (level == FATAL) {
      abort(); // 对齐 Chromium，致命错误中断
    }
    return *this;
  }

private:
  std::stringstream buffer_;
};

extern VLog vlog;
using  base::VLog;
}; // namespace base