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
scp scripts/get_token.sh ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/scripts/get_token.sh
scp scripts/setup_schema.sh ${USERNAME}@${RASP_IP}:/home/${USERNAME}/bms/scripts/setup_schema.sh
