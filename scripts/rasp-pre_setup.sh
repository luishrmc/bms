#!/bin/bash

# NOTE: Create a ssh key and add it to the raspberry pi before running this script,
#       using ssh-keygen and ssh-copy-id commands.

USERNAME="lumac"
RASP_IP="192.168.0.15"

ssh ${USERNAME}@${RASP_IP} "mkdir -p /home/${USERNAME}/bms/bin"
ssh ${USERNAME}@${RASP_IP} "mkdir -p /home/${USERNAME}/bms/scripts"
ssh ${USERNAME}@${RASP_IP} "mkdir -p /home/${USERNAME}/bms/config/influxdb3/core/{data,plugins}"
ssh ${USERNAME}@${RASP_IP} "mkdir -p /home/${USERNAME}/bms/config/influxdb3/explorer/{config,db}"
ssh ${USERNAME}@${RASP_IP} "mkdir -p /home/${USERNAME}/bms/config/grafana/{dashboards,data,provisioning}"
ssh ${USERNAME}@${RASP_IP} "mkdir -p /home/${USERNAME}/bms/config/mosquitto/{config,data,logs}"

scp rasp/docker-compose.yml ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/docker-compose.yml
scp rasp/Dockerfile ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/Dockerfile
scp rasp/entrypoint.sh ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/entrypoint.sh
scp rasp/bms ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/bin/bms
scp scripts/get_token.sh ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/scripts/get_token.sh
scp scripts/setup_schema.sh ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/scripts/setup_schema.sh
scp config/mosquitto/config/mosquitto.conf ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/config/mosquitto/config/mosquitto.conf

ssh ${USERNAME}@${RASP_IP} "chmod +x /home/${USERNAME}/bms/bin/bms"
ssh ${USERNAME}@${RASP_IP} "chmod +x /home/${USERNAME}/bms/scripts/get_token.sh"
ssh ${USERNAME}@${RASP_IP} "chmod +x /home/${USERNAME}/bms/scripts/setup_schema.sh"
ssh ${USERNAME}@${RASP_IP} "chmod +x /home/${USERNAME}/bms/entrypoint.sh"
ssh ${USERNAME}@${RASP_IP} "sudo chown -R 1500:1500 /home/${USERNAME}/bms/config/influxdb3/core/data /home/${USERNAME}/bms/config/influxdb3/core/plugins"
ssh ${USERNAME}@${RASP_IP} "sudo chmod -R u+rwX,g+rwX /home/${USERNAME}/bms/config/influxdb3/core"

echo "Pre-setup completed. You can now SSH into the Raspberry Pi and run the setup scripts."
