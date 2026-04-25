#pragma once
#include <qstring.h>
#include <qdatetime.h>
#include <qhash.h>
#include <qvector.h>
/**
 * @brief 数据质量码定义
 *
 * 工业DCS数据质量码是IEC标准概念：
 * - Good: 数据来自正常通信，值可信
 * - Bad: 通信断开或传感器故障，值不可信
 * - Uncertain: 通信不稳定或值超出量程，值存疑
 *
 * 踩坑经验：
 * - 断线后必须将质量码标记为Bad，UI上数值变灰显示"???"
 * - 绝对不能停留在断线前的旧数值上！
 *   我之前见过一个系统，断线后温度显示还停在85℃，
 *   操作员以为正常，实际反应釜已经超温到120℃了
 */

enum class DataQuality :qint8 {
    Good = 0,
    Bad = 1,
    Uncertain = 2
};
/**
 * @brief 报警状态定义
 *
 * 化工DCS报警级别（ISA-18.2标准）：
 * - 正常(Normal): 无报警
 * - 低低报(LL): 严重偏低，可能引发安全事故
 * - 低报(L): 偏低预警
 * - 高报(H): 偏高预警
 * - 高高报(HH): 严重偏高，可能引发安全事故
 */

enum class AlarmState : quint8 {
    Normal = 0,
    LowLow = 1,
    Low = 2,
    High = 3,
    HighHigh = 4
};

/**
 * @brief 位号数据类型
 *
 * 化工厂常见的信号类型：
 * - AI: 模拟量输入（温度、压力、液位、流量）
 * - AO: 模拟量输出（阀门开度设定）
 * - DI: 数字量输入（泵运行/停止、阀门开/关）
 * - DO: 数字量输出（启停命令）
 * - PID: PID回路（包含PV/SP/OUT三个关联变量）
 */

enum class TagType : quint8 {
    AI = 0,
    AO = 1,
    DI = 2,
    DO = 3,
    PID = 4
};


/**
 * @brief 位号信息结构体
 *
 * 这是整个DCS系统最核心的数据结构，每个测点对应一个TagInfo。
 * 化工厂300个测点，就是300个TagInfo实例。
 *
 * 设计要点：
 * 1. 使用普通类型，线程安全由RealtimeDb的QReadWriteLock保证
 * 2. 静态属性（量程、报警限值）在组态时确定，运行期不变
 * 3. 报警死区(deadband)防止信号在报警限附近来回波动时产生报警风暴
 *
 * 踩坑经验：
 * - 之前用std::atomic成员，结果TagInfo变成不可复制，无法存入QHash
 * - Qt的QReadWriteLock性能足够好，300个点刷新无压力
 */
struct TagInfo {

    quint32 tagId = 0;              // 唯一ID
    QString tagName;                // 位号名，如 "TIC_101"（1号反应釜温度控制）
    QString description;            // 描述，如 "1号反应釜温度"
    QString unit;                   // 工程单位，如 "℃"、"MPa"、"%"
    TagType tagType = TagType::AI;  // 信号类型

    // 实时数据（由RealtimeDb的读写锁保护）
    float currentValue = 0.0f;      // 实时测量值(PV)
    float setPoint = 0.0f;          // 设定值(SP)
    float outputValue = 0.0f;       // 输出值(OUT，阀门开度)
    AlarmState alarmState = AlarmState::Normal; // 报警状态
    DataQuality quality = DataQuality::Good;    // 数据质量码
    qint64 timestamp = 0;           // 时间戳（毫秒）


    // 静态属性（组态时确定，运行期只读）
    float engHigh = 100.0f;         // 量程上限
    float engLow = 0.0f;            // 量程下限
    float highHighLimit = 90.0f;    // 高高报限值
    float highLimit = 80.0f;        // 高报限值
    float lowLimit = 10.0f;         // 低报限值
    float lowLowLimit = 5.0f;       // 低低报限值
    float deadband = 1.0f;          // 报警死区（防止报警风暴的关键参数）

     // Modbus映射（位号与PLC寄存器地址的映射关系）
    int modbusServerAddr = 1;       // Modbus从站地址
    int modbusRegAddr = 0;          // 寄存器起始地址
    int modbusRegCount = 1;         // 占用寄存器数量

    // PID参数（仅PID类型位号有效）
    float kp = 1.0f;                // 比例增益
    float ki = 0.1f;                // 积分时间
    float kd = 0.0f;                // 微分时间
    bool autoMode = true;           // 自动模式标志

    // 便捷访问方法
    float pv() const { return currentValue; }
    float sp() const { return setPoint; }
    float out() const { return outputValue; }
    DataQuality qual() const { return quality; }
    AlarmState alarm() const { return alarmState; }

};



