# Docker-compose for NGINX Instance Manager

1. Edit the `.env` file setting the docker image name and the base64-encoded license
2. Start NGINX Instance Manager
```bash
docker compose -f docker-compose.yaml up -d
```

3. Stop NGINX Instance Manager
```bash
docker compose -f docker-compose.yaml down
```
