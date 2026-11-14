#pragma once
#include <functional>
#include <QByteArray>
#include <HandleBase.h>

// 仅供测试：把各用例的断言放入末端虚函数，生产库不提供回调兼容入口。
template<class Transport>
class CallbackHandle final : public Transport
{
public:
    explicit CallbackHandle(std::function<void(const QByteArray &, RxTime)> callback) : callback_(std::move(callback)) {}
    ~CallbackHandle() override { this->close(); }
private:
    void onData(std::span<const unsigned char> data, RxTime at) override
    {
        callback_(QByteArray::fromRawData(reinterpret_cast<const char *>(data.data()),
                                        static_cast<qsizetype>(data.size())), at);
    }
    const std::function<void(const QByteArray &, RxTime)> callback_;
};
