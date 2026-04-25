#pragma once
enum class LogLevel {
    Error = 0,   // 错误：系统故障，需要立即处理
    Warn = 1,    // 警告：潜在问题，需要关注
    Info = 2,    // 信息：关键业务操作
    Debug = 3    // 调试：详细调试信息
};
