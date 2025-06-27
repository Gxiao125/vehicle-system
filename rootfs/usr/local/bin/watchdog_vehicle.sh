#!/bin/sh

# 更兼容的看门狗脚本
services="can_service body_control powertrain dashboard"

while true; do
    for service in $services; do
        if ! pgrep -x "$service" > /dev/null; then
            echo "$(date) - $service is not running, restarting..." >> /var/log/watchdog.log
            
            case $service in
                "can_service") 
                    /usr/local/bin/can_service >> /var/log/can_service.log 2>&1 & 
                    ;;
                "body_control") 
                    /app/body_control/body_control >> /var/log/body_control.log 2>&1 & 
                    ;;
                "powertrain") 
                    /app/brake_system/powertrain >> /var/log/powertrain.log 2>&1 & 
                    ;;
                "dashboard") 
                    /app/dashboard/dashboard >> /var/log/dashboard.log 2>&1 & 
                    ;;
            esac
        fi
    done
    sleep 30
done

