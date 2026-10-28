
#pragma once

namespace StartupManager
{
    // 查询是否已启用开机自启动
    bool isEnabled();

    // 设置开机自启动状态
    // enabled = true：开启
    // enabled = false：关闭
    bool setEnabled(bool enabled);
}
