#!/bin/bash
# ============================================
# 安全防火墙配置脚本（UFW）
# 功能: 放行 SSH、8877端口、内网段，禁止其他入站
# ============================================

echo "🚀 开始配置防火墙规则..."

# 检查 ufw 是否安装
if ! command -v ufw &> /dev/null; then
    echo "⚙️ 未检测到 UFW，正在安装..."
    sudo apt update -y && sudo apt install ufw -y
fi

#!/bin/bash
set -e

echo "=== 配置 UFW 防火墙 ==="

# 放行 SSH（必须）
sudo ufw allow 22/tcp

# 允许内网访问（防止断连）
sudo ufw allow from 192.168.0.0/16
sudo ufw allow from 10.0.0.0/8
sudo ufw allow from 172.16.0.0/12

# 默认策略
sudo ufw default deny incoming
sudo ufw default allow outgoing

# 启用防火墙
sudo ufw --force enable

#关闭防火墙
#sudo ufw disable

# 查看结果
sudo ufw status verbose

echo "=== 防火墙配置完成 ==="
