# Docker-compose for NGINX Instance Manager

1. Edit the `.env` file setting the docker image name, the base64-encoded license and optionally username and password
```code
NIM_IMAGE=<NGINX_INSTANCE_MANAGER_DOCKER_IMAGE_NAME>
NIM_LICENSE=<BASE64_ENCODED_LICENSE_FILE>
NIM_USERNAME=admin
NIM_PASSWORD=nimadmin
```

2. Start NGINX Instance Manager
```bash
docker compose -f docker-compose.yaml up -d
```

3. Stop NGINX Instance Manager
```bash
docker compose -f docker-compose.yaml down
```
