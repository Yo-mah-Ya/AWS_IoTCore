#!/bin/bash
image_name="awsiot-python:latest"

function blue(){
    echo -e "\033[34m"$1"\033[0m"
}

case $1 in
    "build") blue "Build Docker Images"
	        docker image build -t ${image_name} .;;
    "exec") blue "Execute Docker container"
	        CONTAINER_ID=$(docker container run -itd -p 3000:3000 ${image_name})
            docker container exec -it ${CONTAINER_ID} sh;;
    "rmc") blue "Remove All Docker Containers"
	        docker container ps -aq | xargs docker container rm -f;;
    "rmi") blue "Remove All Docker Images"
	        docker image ls -aq | xargs docker image rm -f;;
    "run") blue "Run Docker container"
	        docker container run --rm ${image_name};;
    *) echo "give me Docker command";;
esac
