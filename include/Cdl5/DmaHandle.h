// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <memory>
#include <system_error>
#include <QString>
#include <HandleBase.h>
#include "DmaConnection.h"

// 连接层：复用 HandleCore 的打开、重连与同步关闭契约。
// 收包、取消、发令及 RX/TX 裁决留为纯虚；未实现前不提供可实例化的采集设备。
class DmaHandle : public Handle<DmaParameters, DmaConnection>
{
    using Base = Handle<DmaParameters, DmaConnection>;
public:
    using Base::Base;

protected:
    ~DmaHandle() override = default;

    std::shared_ptr<DmaConnection> connOpen(const DmaParameters &params) override
    {
        try
        {
            return DmaConnection::open(params);
        }
        catch (const std::system_error &error)
        {
            Q_EMIT logMessage(QString::fromUtf8(error.what()));
            return {};
        }
    }
};
