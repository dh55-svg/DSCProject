#pragma once
#include <qtypes.h>
#include <qvector.h>

struct RawModbusData {
    int serverAddr = 0;
    int startAddr = 0;
    int count = 0;
    quint16 values[128] = {};
};

class IMessageBus {
public:
    virtual ~IMessageBus() = default;
    virtual bool enqueue(const RawModbusData& data) = 0;
    virtual bool dequeue(RawModbusData& data) = 0;
    virtual size_t dequeueBatch(std::vector<RawModbusData>& output, size_t maxCount) = 0;
    virtual bool empty() const = 0;
    virtual size_t size() const = 0;
};
