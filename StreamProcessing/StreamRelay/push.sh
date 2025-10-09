#!/bin/bash

# 定义推流列表
STREAMS=(
"http://278172839.xyz:80/live/Tvheyevfhxla-r/ZxUuNhXwUcasjwieh/65475.ts rtmp://127.0.0.1/live/dazn_la_liga"
"http://278172839.xyz:80/live/Tvheyevfhxla-r/ZxUuNhXwUcasjwieh/65481.ts rtmp://127.0.0.1/live/la_liga_smartbank_tv"
"http://278172839.xyz:80/live/Tvheyevfhxla-r/ZxUuNhXwUcasjwieh/189.ts rtmp://127.0.0.1/live/movistar_liga_de_campeones"
"http://278172839.xyz:80/live/Tvheyevfhxla-r/ZxUuNhXwUcasjwieh/202.ts rtmp://127.0.0.1/live/movistar_deportes"
"http://278172839.xyz:80/live/Tvheyevfhxla-r/ZxUuNhXwUcasjwieh/205.ts rtmp://127.0.0.1/live/dazn_f1"
)

# 保活函数：循环执行 FFmpeg，如果断线自动重启
start_stream() {
    local input="$1"
    local output="$2"

    while true; do
        echo "Starting stream: $input -> $output"
        ffmpeg -re -i "$input" \
            -c:v libx264 -preset veryfast -tune zerolatency -g 100 -keyint_min 100 \
            -c:a aac -f flv "$output"
        
        echo "Stream stopped, restarting in 5 seconds: $output"
        sleep 5
    done
}

# 循环启动每一路推流，后台执行
for s in "${STREAMS[@]}"; do
    input=$(echo $s | awk '{print $1}')
    output=$(echo $s | awk '{print $2}')
    start_stream "$input" "$output" &
done

# 等待所有后台进程
wait
